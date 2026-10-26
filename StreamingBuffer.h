#pragma once
#ifndef STREAMINGBUFFER_H
#define STREAMINGBUFFER_H

#include <QOpenGLFunctions>
#include <QtGlobal>

class StreamingBuffer
{
public:
    struct UploadResult {
        bool ok = false;
        bool bufferRecreated = false;
        bool persistentMapped = false;
    };

    StreamingBuffer() = default;
    ~StreamingBuffer();

    StreamingBuffer(const StreamingBuffer&) = delete;
    StreamingBuffer& operator=(const StreamingBuffer&) = delete;

    bool initialize(GLenum target = GL_ARRAY_BUFFER);
    void destroy();

    GLuint id() const { return m_bufferId; }
    bool isPersistentMapped() const { return m_persistentMapped; }
    qsizetype capacityBytes() const { return m_capacityBytes; }

    UploadResult upload(const void* data, qsizetype byteCount, GLenum fallbackUsage);

private:
    bool createBuffer();
    bool allocatePersistentStorage(qsizetype byteCount);
    qsizetype grownCapacity(qsizetype byteCount) const;

private:
    GLuint m_bufferId = 0;
    GLenum m_target = GL_ARRAY_BUFFER;
    qsizetype m_capacityBytes = 0;
    unsigned char* m_mappedData = nullptr;
    bool m_persistentCapable = false;
    bool m_persistentMapped = false;
};

#endif // STREAMINGBUFFER_H
