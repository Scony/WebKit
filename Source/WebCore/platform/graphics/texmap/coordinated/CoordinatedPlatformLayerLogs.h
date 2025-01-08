namespace WebCore {

void CoordinatedPlatformLayer::log(OptionSet<Change>& m_pendingChanges, RefPtr<CoordinatedBackingStoreProxy>& m_backingStoreProxy, std::unique_ptr<CoordinatedPlatformLayerBuffer>& m_contentsBuffer, TextureMapperLayer* layer) const {
    WTFLogAlways("%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c Layer (%p)",
                 m_pendingChanges.contains(Change::Position) ? 'P' : '-',
                 m_pendingChanges.contains(Change::BoundsOrigin) ? 'B' : '-',
                 m_pendingChanges.contains(Change::AnchorPoint) ? 'A' : '-',
                 m_pendingChanges.contains(Change::Size) ? 'S' : '-',
                 m_pendingChanges.contains(Change::Transform) ? 'T' : '-',
                 m_pendingChanges.contains(Change::ChildrenTransform) ? 'C' : '-',
                 m_pendingChanges.contains(Change::DrawsContent) ? 'D' : '-',
                 m_pendingChanges.contains(Change::MasksToBounds) ? 'M' : '-',
                 m_pendingChanges.contains(Change::Preserves3D) ? '3' : '-',
                 m_pendingChanges.contains(Change::BackfaceVisibility) ? 'F' : '-',
                 m_pendingChanges.contains(Change::Opacity) ? 'O' : '-',
                 m_pendingChanges.contains(Change::Children) ? 'I' : '-',
                 m_pendingChanges.contains(Change::ContentsVisible) ? 'V' : '-',
                 m_pendingChanges.contains(Change::ContentsOpaque) ? 'Q' : '-',
                 m_pendingChanges.contains(Change::ContentsRect) ? 'R' : '-',
                 m_pendingChanges.contains(Change::ContentsRectClipsDescendants) ? 'r' : '-',
                 m_pendingChanges.contains(Change::ContentsClippingRect) ? 'c' : '-',
                 m_pendingChanges.contains(Change::ContentsTiling) ? 't' : '-',
                 m_pendingChanges.contains(Change::ContentsBuffer) ? 'b' : '-',
                 m_pendingChanges.contains(Change::ContentsImage) ? 'i' : '-',
                 m_pendingChanges.contains(Change::ContentsColor) ? 'o' : '-',
                 m_pendingChanges.contains(Change::Filters) ? 'f' : '-',
                 m_pendingChanges.contains(Change::Mask) ? 'm' : '-',
                 m_pendingChanges.contains(Change::Replica) ? 'e' : '-',
                 m_pendingChanges.contains(Change::Backdrop) ? 'd' : '-',
                 m_pendingChanges.contains(Change::BackdropRect) ? 'k' : '-',
                 m_pendingChanges.contains(Change::Animations) ? 'n' : '-',
                 m_pendingChanges.contains(Change::DebugIndicators) ? 'g' : '-',
                 m_pendingChanges.contains(Change::Damage) ? 'G' : '-',
                 m_backingStoreProxy ? 'X' : '-',
                 m_contentsBuffer ? 'U' : '-',
                 layer);
}

}
