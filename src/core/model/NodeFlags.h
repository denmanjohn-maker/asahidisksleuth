#pragma once

#include <QtTypes>

namespace ads {

/// What kind of filesystem object a node is.
enum class NodeKind : quint16 {
    File = 0,
    Directory = 1,
    Symlink = 2,
    Other = 3,
};

/// Packed per-node kind + attribute flags (2 bytes per node; storage matters
/// at millions of nodes). Linux port: dataless/purgeable/firmlinkSkipped are
/// retired (no Linux concept); cloned/sharedEstimate are reserved for the
/// Phase 2 btrfs reflink engine.
class NodeFlags
{
public:
    explicit NodeFlags(NodeKind kind)
        : m_raw(static_cast<quint16>(kind))
    {
    }
    explicit NodeFlags(quint16 raw)
        : m_raw(raw)
    {
    }

    quint16 rawValue() const { return m_raw; }
    NodeKind kind() const { return static_cast<NodeKind>(m_raw & 0b11); }

    bool cloned() const { return has(ClonedBit); }
    void setCloned(bool on) { set(ClonedBit, on); }

    bool sparse() const { return has(SparseBit); }
    void setSparse(bool on) { set(SparseBit, on); }

    bool hardlinked() const { return has(HardlinkedBit); }
    void setHardlinked(bool on) { set(HardlinkedBit, on); }

    /// Directory could not be read; subtree sizes undercount.
    bool accessDenied() const { return has(AccessDeniedBit); }
    void setAccessDenied(bool on) { set(AccessDeniedBit, on); }

    /// Transparently compressed (e.g. btrfs FS_COMPR_FL); physical < logical
    /// is real savings.
    bool compressed() const { return has(CompressedBit); }
    void setCompressed(bool on) { set(CompressedBit, on); }

    /// Mount point onto another volume; not traversed.
    bool otherVolume() const { return has(OtherVolumeBit); }
    void setOtherVolume(bool on) { set(OtherVolumeBit, on); }

    /// Hardlinks/clones of this content exist outside the scanned tree —
    /// deleting everything scanned still can't free the shared bytes.
    bool externalLinks() const { return has(ExternalLinksBit); }
    void setExternalLinks(bool on) { set(ExternalLinksBit, on); }

    /// A clone family's shared bytes were estimated, not exact. (Phase 2.)
    bool sharedEstimate() const { return has(SharedEstimateBit); }
    void setSharedEstimate(bool on) { set(SharedEstimateBit, on); }

    /// Directory already visited via another path (cycle); not re-traversed.
    bool duplicate() const { return has(DuplicateBit); }
    void setDuplicate(bool on) { set(DuplicateBit, on); }

    bool operator==(const NodeFlags &) const = default;

private:
    static constexpr quint16 ClonedBit = 1 << 2;
    static constexpr quint16 SparseBit = 1 << 3;
    static constexpr quint16 HardlinkedBit = 1 << 4;
    static constexpr quint16 AccessDeniedBit = 1 << 7;
    static constexpr quint16 CompressedBit = 1 << 8;
    static constexpr quint16 OtherVolumeBit = 1 << 9;
    static constexpr quint16 ExternalLinksBit = 1 << 10;
    static constexpr quint16 SharedEstimateBit = 1 << 11;
    static constexpr quint16 DuplicateBit = 1 << 12;

    bool has(quint16 bit) const { return m_raw & bit; }
    void set(quint16 bit, bool on)
    {
        if (on)
            m_raw |= bit;
        else
            m_raw &= ~bit;
    }

    quint16 m_raw;
};

} // namespace ads
