#pragma once
#ifndef EARTHVIEW3D_MODELTABLE_H
#define EARTHVIEW3D_MODELTABLE_H

#include "earthview3d/EntityState3D.h"

#include <QByteArray>
#include <QString>

#include <vector>

namespace earthview3d {

/**
 * @brief One row of the model table: which model (or placeholder style) a kind gets.
 */
struct ModelEntry
{
    QString pattern;                 ///< Glob on kindKey (`*`, `?`), whole-key match
    QString file;                    ///< Model file (.osgb, .gltf, …); empty = placeholder shape
    double scale = 1.0;
    double headingOffsetDeg = 0.0;   ///< Model-space correction applied before the entity attitude
    double pitchOffsetDeg = 0.0;
    double rollOffsetDeg = 0.0;
    bool clampToTerrain = false;     ///< Clamp this kind even if the host did not ask to
    int shape = -1;                  ///< Placeholder override (-1 = use EntityState3D::shape)
};

/**
 * @brief kindKey → model mapping loaded from models.json (roadmap E3).
 *
 * JSON:
 * @code
 * { "iconBelowPixels": 12,
 *   "models": [ { "kind": "1.1.225.1.1.*", "file": "t72.osgb", "scale": 1.0,
 *                 "hpr": [90, 0, 0], "clampToTerrain": true, "shape": 2 } ] }
 * @endcode
 * Matching: the most specific pattern wins (most literal characters); ties go to the earlier
 * entry. Relative model files resolve against the table file's folder.
 */
class ModelTable
{
public:
    /// Models smaller than this on screen (pixels) are drawn as an icon instead (LOD).
    double iconBelowPixels() const { return m_iconBelowPixels; }
    const std::vector<ModelEntry>& entries() const { return m_entries; }
    bool isEmpty() const { return m_entries.empty(); }

    /// Best entry for @p kindKey, or nullptr.
    const ModelEntry* match(const QString& kindKey) const;
    /// Absolute model path for an entry (empty when the entry has no file).
    QString resolvedFile(const ModelEntry& entry) const;

    static bool fromJson(const QByteArray& json, const QString& baseDir, ModelTable* table,
                         QString* error = nullptr);
    static bool fromJsonFile(const QString& path, ModelTable* table, QString* error = nullptr);

    /// Number of literal (non-wildcard) characters: the pattern's specificity.
    static int specificity(const QString& pattern);

private:
    std::vector<ModelEntry> m_entries;
    QString m_baseDir;
    double m_iconBelowPixels = 12.0;
};

/**
 * @brief Everything the view needs to draw one entity, resolved from its state and the table.
 */
struct ResolvedStyle
{
    QString modelFile;       ///< Empty: draw the placeholder shape
    int shape = 0;
    double scale = 1.0;
    double headingOffsetDeg = 0.0;
    double pitchOffsetDeg = 0.0;
    double rollOffsetDeg = 0.0;
    bool clampToTerrain = false;
    std::uint32_t rgba = 0xFFFFFFFFu;
    QString label;           ///< Label text (falls back to "#id")
    bool visible = true;
};

/// Resolve how @p state is drawn (table may be null). GL-free; unit tested.
ResolvedStyle resolveStyle(const EntityState3D& state, const ModelTable* table);

} // namespace earthview3d

#endif // EARTHVIEW3D_MODELTABLE_H
