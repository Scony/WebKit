namespace WebKit {
using namespace WebCore;

static void log(const FloatRect& clipRect, Damage& frameDamage, const Damage& damageSinceLastSurfaceUse)
{
    WTFLogAlways("ThreadedCompositor::paintToCurrentGLContext() clipRect:(%f,%f,%f,%f) frameDamage:(v?%d,#%d,%d,%d,%d,%d) damageSinceLastSurfaceUse:(v?%d,#%d,%d,%d,%d,%d)",
                 clipRect.x(),
                 clipRect.y(),
                 clipRect.width(),
                 clipRect.height(),
                 !frameDamage.isInvalid(),
                 (int)frameDamage.rects().size(),
                 frameDamage.bounds().x(),
                 frameDamage.bounds().y(),
                 frameDamage.bounds().width(),
                 frameDamage.bounds().height(),
                 !damageSinceLastSurfaceUse.isInvalid(),
                 (int)damageSinceLastSurfaceUse.rects().size(),
                 damageSinceLastSurfaceUse.bounds().x(),
                 damageSinceLastSurfaceUse.bounds().y(),
                 damageSinceLastSurfaceUse.bounds().width(),
                 damageSinceLastSurfaceUse.bounds().height()
                 );
}

}
