#include "StreamingBuffer.h"
#include "FrameProfiler.h"
#include "OpenGLRuntime.h"

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QtOpenGL/QOpenGLFunctions_4_5_Core>
#include <QtOpenGL/QOpenGLVersionFunctionsFactory>
#include <algorithm>
#include <cstring>

#ifndef GL_MAP_PERSISTENT_BIT
#define GL_MAP_PERSISTENT_BIT 0x0040
#endif

#ifndef GL_MAP_COHERENT_BIT
#define GL_MAP_COHERENT_BIT 0x0080
#endif

#ifndef GL_DYNAMIC_STORAGE_BIT
#define GL_DYNAMIC_STORAGE_BIT 0x0100
#endif

namespace {
constexpr qsizetype kMinimumPersistentCapacity = 256 * 1024;

QOpenGLFunctions_4_5_Core* functions45(QOpenGLContext* context)
{
    if (!context)
        return nullptr;

    QOpenGLFunctions_4_5_Core* f45 =
        QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_5_Core>(context);
    if (f45) {
        f45->initializeOpenGLFunctions();
    }
    return f45;
}
}

StreamingBuffer::~StreamingBuffer()
{
    destroy();
}

bool StreamingBuffer::initialize(GLenum target)
{
    m_target = target;
    if (m_bufferId != 0)
        return true;

    return createBuffer();
}

bool StreamingBuffer::createBuffer()
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context)
        return false;

    m_persistentCapable = OpenGLRuntime::supportsPersistentMappedBuffers(context);

    if (m_persistentCapable) {
        QOpenGLFunctions_4_5_Core* f45 = functions45(context);
        if (f45) {
            f45->glCreateBuffers(1, &m_bufferId);
        }
    }

    if (m_bufferId == 0) {
        QOpenGLExtraFunctions* f = context->extraFunctions();
        if (!f)
            return false;

        f->initializeOpenGLFunctions();
        f->glGenBuffers(1, &m_bufferId);
        m_persistentCapable = false;
    }

    return m_bufferId != 0;
}

void StreamingBuffer::destroy()
{
    if (m_bufferId == 0)
        return;

    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* f = context ? context->extraFunctions() : nullptr;
    QOpenGLFunctions_4_5_Core* f45 = functions45(context);

    if (m_mappedData && f45) {
        f45->glUnmapNamedBuffer(m_bufferId);
    }

    if (f) {
        f->glDeleteBuffers(1, &m_bufferId);
    }

    m_bufferId = 0;
    m_capacityBytes = 0;
    m_mappedData = nullptr;
    m_persistentCapable = false;
    m_persistentMapped = false;
}

qsizetype StreamingBuffer::grownCapacity(qsizetype byteCount) const
{
    qsizetype capacity = std::max(kMinimumPersistentCapacity, m_capacityBytes);
    while (capacity < byteCount) {
        capacity *= 2;
    }
    return capacity;
}

bool StreamingBuffer::allocatePersistentStorage(qsizetype byteCount)
{
    if (!m_persistentCapable || m_bufferId == 0 || byteCount <= 0)
        return false;

    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLFunctions_4_5_Core* f45 = functions45(context);
    if (!context || !context->functions() || !f45)
        return false;

    const qsizetype capacity = grownCapacity(byteCount);
    const GLbitfield flags = GL_MAP_WRITE_BIT
        | GL_MAP_PERSISTENT_BIT
        | GL_MAP_COHERENT_BIT
        | GL_DYNAMIC_STORAGE_BIT;

    f45->glNamedBufferStorage(m_bufferId, capacity, nullptr, flags);
    if (context->functions()->glGetError() != GL_NO_ERROR)
        return false;

    m_mappedData = static_cast<unsigned char*>(
        f45->glMapNamedBufferRange(m_bufferId, 0, capacity, flags));
    if (!m_mappedData)
        return false;

    m_capacityBytes = capacity;
    m_persistentMapped = true;
    FrameProfiler::recordCount(QStringLiteral("streamingBuffer.persistentAllocations"));
    FrameProfiler::recordCount(QStringLiteral("streamingBuffer.persistentCapacityBytes"), capacity);
    return true;
}

StreamingBuffer::UploadResult StreamingBuffer::upload(
    const void* data,
    qsizetype byteCount,
    GLenum fallbackUsage)
{
    UploadResult result;
    if (byteCount <= 0) {
        result.ok = true;
        result.persistentMapped = m_persistentMapped;
        return result;
    }

    if (!initialize(m_target))
        return result;

    if (m_persistentCapable && !m_persistentMapped) {
        if (!allocatePersistentStorage(byteCount)) {
            destroy();
            if (!initialize(m_target))
                return result;
            result.bufferRecreated = true;
            m_persistentCapable = false;
        }
    }
    else if (m_persistentMapped && byteCount > m_capacityBytes) {
        destroy();
        if (!initialize(m_target))
            return result;
        result.bufferRecreated = true;
        if (!allocatePersistentStorage(byteCount)) {
            destroy();
            if (!initialize(m_target))
                return result;
            m_persistentCapable = false;
        }
    }

    if (m_persistentMapped && byteCount <= m_capacityBytes && m_mappedData) {
        std::memcpy(m_mappedData, data, static_cast<size_t>(byteCount));
        FrameProfiler::recordCount(QStringLiteral("streamingBuffer.persistentUploadBytes"), byteCount);
        result.ok = true;
        result.persistentMapped = true;
        return result;
    }

    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* f = context ? context->extraFunctions() : nullptr;
    if (!f)
        return result;

    f->glBindBuffer(m_target, m_bufferId);
    f->glBufferData(m_target, byteCount, data, fallbackUsage);
    f->glBindBuffer(m_target, 0);
    FrameProfiler::recordCount(QStringLiteral("streamingBuffer.fallbackUploadBytes"), byteCount);

    result.ok = true;
    result.persistentMapped = false;
    return result;
}
