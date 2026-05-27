#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFileInfo>
#include <QtCore/QPoint>
#include <QtCore/QProcess>
#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <QtGui/QColor>
#include <QtGui/QCursor>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QBackingStore>
#include <QtGui/QKeyEvent>
#include <QtGui/QLinearGradient>
#include <QtGui/QMouseEvent>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPaintEvent>
#include <QtGui/QRegion>
#include <QtGui/QResizeEvent>
#include <QtGui/QScreen>
#include <QtGui/QSurfaceFormat>
#include <QtGui/QWindow>
#ifdef RIDUX_DIRECT_ONLY
#include <QtCore/QUrl>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlError>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>
#endif
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include <QtOpenGLWidgets/QOpenGLWidget>

#include <cmath>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include <GL/gl.h>

static bool g_directKmsShell = false;

static const char *kRiduxStyle = R"CSS(
QWidget {
    background: #f4f6f8;
    color: #14181d;
    font-family: "DejaVu Sans", "Arial", sans-serif;
    font-size: 12px;
}
QMainWindow {
    background: #f4f6f8;
}
QLabel#title {
    font-size: 19px;
    font-weight: 800;
    color: #111820;
}
QLabel#subtitle {
    color: #5c6673;
}
QFrame#surface {
    background: #ffffff;
    border: 1px solid #d4dae2;
    border-radius: 8px;
}
QFrame#panelRoot {
    background: #20242b;
    border-bottom: 1px solid #48505b;
}
QFrame#dockRoot {
    background: #242a31;
    border: 1px solid #68717d;
    border-radius: 8px;
}
QFrame#launcherRoot {
    background: #f9fafb;
    border: 1px solid #ccd3dc;
    border-radius: 8px;
}
QLabel#brand {
    background: transparent;
    color: #f7f9fb;
    font-size: 14px;
    font-weight: 900;
}
QLabel#clock {
    background: transparent;
    color: #e6edf5;
    font-weight: 800;
}
QPushButton {
    min-height: 30px;
    padding: 0 12px;
    border-radius: 6px;
    border: 1px solid #b8c1cc;
    background: #ffffff;
    color: #15191f;
    font-weight: 650;
}
QPushButton:hover {
    background: #eaf3ff;
    border-color: #6c9bd2;
}
QPushButton:pressed {
    background: #d8e8f9;
}
QPushButton#panelButton, QPushButton#dockButton {
    color: #f7f9fb;
    background: #313943;
    border-color: #616c79;
}
QPushButton#panelButton:hover, QPushButton#dockButton:hover {
    background: #285f8f;
    border-color: #85b8e7;
}
QPushButton#dockButton {
    min-width: 76px;
    min-height: 42px;
}
QTableWidget, QListWidget {
    background: #ffffff;
    border: 1px solid #d4dae2;
    border-radius: 6px;
    selection-background-color: #dceefe;
    selection-color: #101419;
}
QHeaderView::section {
    background: #e7ecf2;
    border: 0;
    border-right: 1px solid #d4dae2;
    padding: 5px;
    font-weight: 700;
}
QProgressBar {
    border: 1px solid #c5cbd3;
    border-radius: 5px;
    background: #edf1f5;
    text-align: center;
}
QProgressBar::chunk {
    border-radius: 4px;
    background: #2f7fc5;
}
)CSS";

static bool envTruthy(const char *name)
{
    const QByteArray value = qgetenv(name);
    if (value.isEmpty())
        return false;
    const QByteArray lower = value.toLower();
    return lower != "0" && lower != "false" && lower != "no" && lower != "off";
}

static void applyDirectPalette(QApplication &app)
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(244, 246, 248));
    palette.setColor(QPalette::WindowText, QColor(20, 24, 29));
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::AlternateBase, QColor(232, 237, 243));
    palette.setColor(QPalette::Text, QColor(20, 24, 29));
    palette.setColor(QPalette::Button, QColor(255, 255, 255));
    palette.setColor(QPalette::ButtonText, QColor(21, 25, 31));
    palette.setColor(QPalette::Highlight, QColor(47, 127, 197));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    app.setPalette(palette);
    app.setFont(QFont("DejaVu Sans", 10));
}

static void putenvIfEmpty(const char *name, const char *value)
{
    if (qEnvironmentVariableIsEmpty(name))
        qputenv(name, value);
}

static bool containsCi(const QByteArray &value, const char *needle)
{
    return value.toLower().contains(QByteArray(needle).toLower());
}

static bool rendererLooksSoftware(const QByteArray &probe)
{
    return containsCi(probe, "llvmpipe") ||
           containsCi(probe, "softpipe") ||
           containsCi(probe, "swrast") ||
           containsCi(probe, "kms_swrast") ||
           containsCi(probe, "lavapipe") ||
           containsCi(probe, "software rasterizer");
}

static bool rendererLooksHardwareMesa(const QByteArray &probe)
{
    return containsCi(probe, "virgl") ||
           containsCi(probe, "svga3d") ||
           containsCi(probe, "vmware") ||
           containsCi(probe, "vmwgfx") ||
           containsCi(probe, "iris") ||
           containsCi(probe, "intel") ||
           containsCi(probe, "radeon") ||
           containsCi(probe, "radeonsi") ||
           containsCi(probe, "amdgpu") ||
           containsCi(probe, "nouveau") ||
           containsCi(probe, "nvidia") ||
           containsCi(probe, "zink");
}

static bool logCurrentHardwareRenderer(const char *surface)
{
    const bool requireHardware = envTruthy("RIDUX_REQUIRE_HARDWARE_GL");
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        if (requireHardware) {
            std::fprintf(stderr, "[ridux-shell] rejected GL renderer surface=%s reason=no-current-context\n",
                         surface ? surface : "?");
            return false;
        }
        std::fprintf(stderr, "[ridux-shell] GL check skipped surface=%s reason=no-current-context hardware-required=0\n",
                     surface ? surface : "?");
        return true;
    }
    QOpenGLFunctions *f = ctx->functions();
    if (!f) {
        if (requireHardware) {
            std::fprintf(stderr, "[ridux-shell] rejected GL renderer surface=%s reason=no-functions\n",
                         surface ? surface : "?");
            return false;
        }
        std::fprintf(stderr, "[ridux-shell] GL check skipped surface=%s reason=no-functions hardware-required=0\n",
                     surface ? surface : "?");
        return true;
    }
    f->initializeOpenGLFunctions();
    const GLubyte *vendor = f->glGetString(GL_VENDOR);
    const GLubyte *renderer = f->glGetString(GL_RENDERER);
    const GLubyte *version = f->glGetString(GL_VERSION);
    const GLubyte *shading = f->glGetString(GL_SHADING_LANGUAGE_VERSION);
    const QByteArray vendorName = vendor ? QByteArray(reinterpret_cast<const char *>(vendor)) : QByteArray();
    const QByteArray rendererName = renderer ? QByteArray(reinterpret_cast<const char *>(renderer)) : QByteArray();
    const QByteArray versionName = version ? QByteArray(reinterpret_cast<const char *>(version)) : QByteArray();
    const QByteArray shadingName = shading ? QByteArray(reinterpret_cast<const char *>(shading)) : QByteArray();
    const QByteArray probe = vendorName + QByteArray(" ") + rendererName + QByteArray(" ") + versionName;

    std::fprintf(stderr,
                 "[ridux-shell] GL info surface=%s vendor=%s renderer=%s version=%s glsl=%s\n",
                 surface ? surface : "?",
                 vendorName.constData(), rendererName.constData(),
                 versionName.constData(), shadingName.constData());
    if (requireHardware && (rendererName.isEmpty() || rendererLooksSoftware(probe) ||
                            !rendererLooksHardwareMesa(probe))) {
        std::fprintf(stderr,
                     "[ridux-shell] rejected software/unknown GL renderer surface=%s vendor=%s renderer=%s version=%s\n",
                     surface ? surface : "?",
                     vendorName.constData(), rendererName.constData(), versionName.constData());
        return false;
    }
    if (requireHardware) {
        std::fprintf(stderr,
                     "[ridux-mesa-real] renderer=%s surface=%s qtquick-scenegraph=opengl status=hardware-required accepted\n",
                     rendererName.constData(), surface ? surface : "?");
    } else {
        std::fprintf(stderr, "[ridux-shell] GL renderer accepted without hardware requirement surface=%s\n",
                     surface ? surface : "?");
    }
    return true;
}

#ifdef RIDUX_DIRECT_ONLY
static QByteArray readFileBytes(const QString &path)
{
    QByteArray out;
    const QByteArray native = path.toLocal8Bit();
    FILE *file = std::fopen(native.constData(), "rb");
    if (!file) {
        std::fprintf(stderr, "[ridux-shell] qml fopen failed path=%s errno=%d %s\n",
                     native.constData(), errno, std::strerror(errno));
        return out;
    }
    char buffer[4096];
    for (;;) {
        const size_t n = std::fread(buffer, 1, sizeof(buffer), file);
        if (n > 0)
            out.append(buffer, int(n));
        if (n < sizeof(buffer)) {
            if (std::ferror(file)) {
                std::fprintf(stderr, "[ridux-shell] qml fread failed path=%s errno=%d %s\n",
                             native.constData(), errno, std::strerror(errno));
                out.clear();
            }
            break;
        }
    }
    std::fclose(file);
    std::fprintf(stderr, "[ridux-shell] qml loaded bytes=%lld path=%s\n",
                 static_cast<long long>(out.size()), native.constData());
    return out;
}

static void logQmlErrors(const QList<QQmlError> &errors)
{
    for (const QQmlError &error : errors) {
        const QByteArray url = error.url().toString().toLocal8Bit();
        const QByteArray desc = error.description().toLocal8Bit();
        std::fprintf(stderr, "[ridux-shell] qml error url=%s line=%d column=%d desc=%s\n",
                     url.constData(), error.line(), error.column(), desc.constData());
    }
}

static bool waitForQmlReady(QQmlComponent &component)
{
    for (int i = 0; component.isLoading() && i < 500; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (!component.isReady()) {
        std::fprintf(stderr, "[ridux-shell] qml component not ready status=%d progress=%.3f\n",
                     int(component.status()), component.progress());
        logQmlErrors(component.errors());
        return false;
    }
    return true;
}

static QStringList riduxQmlImportPaths(const QFileInfo &qmlInfo)
{
    QStringList paths;
    paths << QStringLiteral("/usr/lib/x86_64-linux-gnu/qt6/qml")
          << QStringLiteral("/usr/lib/qt6/qml")
          << QStringLiteral("/usr/share/qt6/qml")
          << QStringLiteral("/opt/kde-plasma/usr/lib/x86_64-linux-gnu/qt6/qml");
    const QString shellDir = qmlInfo.absolutePath();
    if (!shellDir.isEmpty())
        paths << shellDir;
    paths.removeDuplicates();
    return paths;
}

static void configureQmlEngine(QQuickView &window, const QFileInfo &qmlInfo)
{
    const QStringList importPaths = riduxQmlImportPaths(qmlInfo);
    const QByteArray importEnv = importPaths.join(QLatin1Char(':')).toLocal8Bit();
    qputenv("QML2_IMPORT_PATH", importEnv);
    qputenv("QML_IMPORT_PATH", importEnv);
    qputenv("QT_QUICK_CONTROLS_STYLE", QByteArrayLiteral("Basic"));
    QDir::setCurrent(qmlInfo.absolutePath());
    window.engine()->setImportPathList(importPaths);
    window.engine()->addPluginPath(QStringLiteral("/usr/lib/x86_64-linux-gnu/qt6/qml"));
    window.engine()->addPluginPath(QStringLiteral("/usr/lib/qt6/qml"));
    std::fprintf(stderr, "[ridux-shell] qml imports=%s cwd=%s\n",
                 importEnv.constData(), QDir::currentPath().toLocal8Bit().constData());
}
#endif

static bool argEquals(int argc, char **argv, const char *flag)
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String(flag))
            return true;
    }
    return false;
}

static bool directKmsRequested(int argc, char **argv)
{
    if (envTruthy("RIDUX_QT_DIRECT_KMS"))
        return true;
    if (argEquals(argc, argv, "--direct-kms"))
        return true;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]).toLower();
        if (arg == "--backend=eglfs" || arg == "--session=eglfs")
            return true;
    }
    return false;
}

static void ensureRiduxEnvironment(int argc, char **argv)
{
    const bool directKms = directKmsRequested(argc, argv);
    if (directKms) {
        putenvIfEmpty("QT_QPA_PLATFORM", "eglfs");
        putenvIfEmpty("QT_QPA_EGLFS_INTEGRATION", "eglfs_kms");
        putenvIfEmpty("QT_QPA_EGLFS_KMS_CONFIG", "/etc/qt6/eglfs-kms.json");
        putenvIfEmpty("QT_QPA_EGLFS_ALWAYS_SET_MODE", "1");
        putenvIfEmpty("QT_QPA_EGLFS_FORCEVSYNC", "1");
        putenvIfEmpty("QT_QPA_EGLFS_HIDECURSOR", "0");
        putenvIfEmpty("QT_QPA_EGLFS_NO_LIBINPUT", "1");
        putenvIfEmpty("QT_QPA_GENERIC_PLUGINS", "evdevmouse:/dev/input/event1,evdevkeyboard:/dev/input/event0");
        putenvIfEmpty("QT_QPA_EVDEV_MOUSE_PARAMETERS", "/dev/input/event1");
        putenvIfEmpty("QT_QPA_EVDEV_KEYBOARD_PARAMETERS", "/dev/input/event0");
        putenvIfEmpty("QT_QPA_FB_DRM", "1");
        putenvIfEmpty("QT_PLUGIN_PATH", "/usr/lib/x86_64-linux-gnu/qt6/plugins:/usr/lib/qt6/plugins");
        putenvIfEmpty("QT_QPA_PLATFORM_PLUGIN_PATH", "/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms:/usr/lib/qt6/plugins/platforms");
        putenvIfEmpty("QT_OPENGL", "es2");
        putenvIfEmpty("QSG_RHI_BACKEND", "opengl");
        putenvIfEmpty("QML2_IMPORT_PATH", "/usr/lib/x86_64-linux-gnu/qt6/qml:/usr/lib/qt6/qml:/opt/kde-plasma/usr/lib/x86_64-linux-gnu/qt6/qml");
        putenvIfEmpty("QML_IMPORT_PATH", "/usr/lib/x86_64-linux-gnu/qt6/qml:/usr/lib/qt6/qml:/opt/kde-plasma/usr/lib/x86_64-linux-gnu/qt6/qml");
        putenvIfEmpty("QSG_INFO", "1");
        putenvIfEmpty("EGL_PLATFORM", "gbm");
        putenvIfEmpty("GBM_BACKEND", "drm");
        putenvIfEmpty("LIBGL_ALWAYS_SOFTWARE", "0");
        putenvIfEmpty("RIDUX_REQUIRE_HARDWARE_GL", "1");
        putenvIfEmpty("XCURSOR_THEME", "Adwaita");
        putenvIfEmpty("XCURSOR_SIZE", "28");
        putenvIfEmpty("XCURSOR_PATH", "/usr/share/icons:/usr/share/pixmaps");
        return;
    } else {
        putenvIfEmpty("QT_QPA_PLATFORM", "wayland");
        putenvIfEmpty("WAYLAND_DISPLAY", "wayland-0");
    }
    putenvIfEmpty("XDG_CURRENT_DESKTOP", "Ridux");
    putenvIfEmpty("DESKTOP_SESSION", "ridux");
    putenvIfEmpty("QT_PLUGIN_PATH", "/usr/lib/x86_64-linux-gnu/qt6/plugins:/usr/lib/qt6/plugins");
    putenvIfEmpty("QT_QPA_PLATFORM_PLUGIN_PATH", "/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms:/usr/lib/qt6/plugins/platforms");
    putenvIfEmpty("QT_OPENGL", "desktop");
    putenvIfEmpty("QSG_RHI_BACKEND", "opengl");
    putenvIfEmpty("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
}

static QRect primaryGeometry()
{
#ifndef RIDUX_DIRECT_ONLY
    QScreen *screen = QApplication::primaryScreen();
#else
    QScreen *screen = QGuiApplication::primaryScreen();
#endif
    return screen ? screen->geometry() : QRect(0, 0, 1024, 768);
}

static void startProgram(const QString &program)
{
    QProcess::startDetached(program, QStringList());
}

class DirectKmsRasterWindow final : public QWindow {
public:
    DirectKmsRasterWindow()
        : store_(new QBackingStore(this))
    {
        std::fprintf(stderr, "[ridux-shell] DirectKmsWindow construct\n");
        setTitle(QStringLiteral("RiduxOS Direct KMS Shell"));
        setFlags(Qt::FramelessWindowHint);
        setSurfaceType(QSurface::RasterSurface);
        resize(initialSize());
        QObject::connect(&timer_, &QTimer::timeout, this, [this]() {
            phase_ += 0.055;
            renderNow();
        });
        timer_.start(16);
    }

protected:
    void exposeEvent(QExposeEvent *) override
    {
        renderNow();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        store_->resize(event->size());
        renderNow();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        for (int i = 0; i < navRects_.size(); ++i) {
            if (navRects_[i].contains(event->pos())) {
                page_ = i;
                renderNow();
                return;
            }
        }
    }

private:
    static void glyphRows(QChar ch, uint8_t out[7])
    {
        for (int i = 0; i < 7; ++i)
            out[i] = 0;
        switch (ch.toUpper().unicode()) {
        case 'A': { const uint8_t r[7] = {14,17,17,31,17,17,17}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'B': { const uint8_t r[7] = {30,17,17,30,17,17,30}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'C': { const uint8_t r[7] = {14,17,16,16,16,17,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'D': { const uint8_t r[7] = {30,17,17,17,17,17,30}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'E': { const uint8_t r[7] = {31,16,16,30,16,16,31}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'F': { const uint8_t r[7] = {31,16,16,30,16,16,16}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'G': { const uint8_t r[7] = {14,17,16,23,17,17,15}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'H': { const uint8_t r[7] = {17,17,17,31,17,17,17}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'I': { const uint8_t r[7] = {14,4,4,4,4,4,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'J': { const uint8_t r[7] = {7,2,2,2,18,18,12}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'K': { const uint8_t r[7] = {17,18,20,24,20,18,17}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'L': { const uint8_t r[7] = {16,16,16,16,16,16,31}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'M': { const uint8_t r[7] = {17,27,21,21,17,17,17}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'N': { const uint8_t r[7] = {17,25,21,19,17,17,17}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'O': { const uint8_t r[7] = {14,17,17,17,17,17,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'P': { const uint8_t r[7] = {30,17,17,30,16,16,16}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'Q': { const uint8_t r[7] = {14,17,17,17,21,18,13}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'R': { const uint8_t r[7] = {30,17,17,30,20,18,17}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'S': { const uint8_t r[7] = {15,16,16,14,1,1,30}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'T': { const uint8_t r[7] = {31,4,4,4,4,4,4}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'U': { const uint8_t r[7] = {17,17,17,17,17,17,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'V': { const uint8_t r[7] = {17,17,17,17,17,10,4}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'W': { const uint8_t r[7] = {17,17,17,21,21,21,10}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'X': { const uint8_t r[7] = {17,17,10,4,10,17,17}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'Y': { const uint8_t r[7] = {17,17,10,4,4,4,4}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'Z': { const uint8_t r[7] = {31,1,2,4,8,16,31}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '0': { const uint8_t r[7] = {14,17,19,21,25,17,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '1': { const uint8_t r[7] = {4,12,4,4,4,4,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '2': { const uint8_t r[7] = {14,17,1,2,4,8,31}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '3': { const uint8_t r[7] = {30,1,1,14,1,1,30}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '4': { const uint8_t r[7] = {2,6,10,18,31,2,2}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '5': { const uint8_t r[7] = {31,16,16,30,1,1,30}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '6': { const uint8_t r[7] = {6,8,16,30,17,17,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '7': { const uint8_t r[7] = {31,1,2,4,8,8,8}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '8': { const uint8_t r[7] = {14,17,17,14,17,17,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '9': { const uint8_t r[7] = {14,17,17,15,1,2,12}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case ':': { const uint8_t r[7] = {0,4,4,0,4,4,0}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '/': { const uint8_t r[7] = {1,1,2,4,8,16,16}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '-': { const uint8_t r[7] = {0,0,0,14,0,0,0}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '.': { const uint8_t r[7] = {0,0,0,0,0,12,12}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        default: break;
        }
    }

    static void drawBitmapText(QPainter &p, int x, int y, const QString &text, int scale, const QColor &color, int maxWidth = 0)
    {
        int cx = x;
        const int limit = maxWidth > 0 ? x + maxWidth : 0x3fffffff;
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (QChar ch : text) {
            uint8_t rows[7];
            if (ch == '\n') {
                cx = x;
                y += 9 * scale;
                continue;
            }
            if (ch.isSpace()) {
                cx += 4 * scale;
                continue;
            }
            if (cx + 5 * scale > limit)
                break;
            glyphRows(ch, rows);
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 5; ++col) {
                    if (rows[row] & (1u << (4 - col)))
                        p.drawRect(cx + col * scale, y + row * scale, scale, scale);
                }
            }
            cx += 6 * scale;
        }
    }

    static QSize initialSize()
    {
        QScreen *screen = QGuiApplication::primaryScreen();
        return screen ? screen->geometry().size() : QSize(1024, 768);
    }

    void drawButton(QPainter &p, const QRect &r, const QString &text, bool active)
    {
        p.setPen(QPen(active ? QColor(105, 157, 209) : QColor(92, 103, 118), 1));
        p.setBrush(active ? QColor(42, 93, 139) : QColor(45, 52, 61));
        p.drawRoundedRect(r, 6, 6);
        const int tw = text.size() * 12;
        drawBitmapText(p, r.center().x() - tw / 2, r.center().y() - 7, text, 2, QColor(245, 248, 252), r.width() - 10);
    }

    void drawMetric(QPainter &p, const QRect &r, const QString &label, const QString &value, const QString &detail)
    {
        p.setPen(QPen(QColor(198, 207, 217), 1));
        p.setBrush(QColor(255, 255, 255));
        p.drawRoundedRect(r, 7, 7);
        drawBitmapText(p, r.left() + 14, r.top() + 12, label, 2, QColor(75, 84, 96), r.width() - 28);
        drawBitmapText(p, r.left() + 14, r.top() + 36, value, 4, QColor(15, 22, 30), r.width() - 28);
        drawBitmapText(p, r.left() + 14, r.top() + 80, detail, 2, QColor(91, 101, 113), r.width() - 28);
    }

    void drawPulse(QPainter &p, const QRect &r)
    {
        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0.0, QColor(249, 251, 252));
        bg.setColorAt(1.0, QColor(219, 228, 236));
        p.fillRect(r, bg);
        p.setPen(QPen(QColor(165, 176, 188, 70), 1));
        for (int x = r.left() + 24; x < r.right(); x += 44)
            p.drawLine(x, r.top(), x, r.bottom());
        for (int y = r.top() + 24; y < r.bottom(); y += 44)
            p.drawLine(r.left(), y, r.right(), y);

        const QPointF center(r.center().x(), r.center().y() + 12);
        const QColor colors[] = {
            QColor(47, 127, 197, 185),
            QColor(38, 156, 119, 170),
            QColor(217, 143, 49, 160),
            QColor(178, 80, 124, 148)
        };
        p.setRenderHint(QPainter::Antialiasing, true);
        for (int i = 0; i < 7; ++i) {
            const qreal t = phase_ + i * 0.66;
            const qreal radius = 24 + 10 * std::sin(t * 1.35);
            const QPointF c(center.x() + std::cos(t) * (r.width() * 0.22),
                            center.y() + std::sin(t * 0.82) * (r.height() * 0.17));
            QColor color = colors[i % 4];
            p.setPen(QPen(color.darker(124), 1));
            p.setBrush(color);
            p.drawEllipse(c, radius, radius);
        }
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    void drawDashboard(QPainter &p, const QRect &area)
    {
        drawBitmapText(p, area.left() + 4, area.top(), QStringLiteral("RIDUXOS DIRECT GPU SHELL"), 4, QColor(16, 23, 31), area.width() - 8);
        drawBitmapText(p, area.left() + 5, area.top() + 42,
                       QStringLiteral("QT GUI FULLSCREEN SESSION ON EGLFS MESA GBM AND KMS /DEV/DRI/CARD0"),
                       2, QColor(86, 96, 108), area.width() - 10);

        const int cardW = (area.width() - 24) / 2;
        const int top = area.top() + 78;
        drawMetric(p, QRect(area.left(), top, cardW, 112), QStringLiteral("Display"), QStringLiteral("KMS"),
                   QStringLiteral("The shell owns the primary scanout through the Ridux DRM node."));
        drawMetric(p, QRect(area.left() + cardW + 24, top, cardW, 112), QStringLiteral("Renderer"), QStringLiteral("Mesa"),
                   QStringLiteral("VirtualBox SVGA3D path is active through vmwgfx commands."));
        drawMetric(p, QRect(area.left(), top + 130, cardW, 112), QStringLiteral("Runtime"), QStringLiteral("Qt 6"),
                   QStringLiteral("QGuiApplication, QWindow and QBackingStore avoid the unstable QWidget path."));
        drawMetric(p, QRect(area.left() + cardW + 24, top + 130, cardW, 112), QStringLiteral("Events"), QStringLiteral("eventfd"),
                   QStringLiteral("poll, eventfd and QThread are running in the Linux ABI layer."));

        QRect pulse(area.left(), top + 266, area.width(), qMax(160, area.bottom() - top - 276));
        p.setPen(QPen(QColor(198, 207, 217), 1));
        p.setBrush(QColor(255, 255, 255));
        p.drawRoundedRect(pulse, 7, 7);
        drawPulse(p, pulse.adjusted(1, 1, -1, -1));
        drawBitmapText(p, pulse.left() + 16, pulse.top() + 14, QStringLiteral("LIVE QT RASTER SURFACE"), 2, QColor(20, 26, 34), pulse.width() - 32);
    }

    void drawApps(QPainter &p, const QRect &area)
    {
        drawBitmapText(p, area.left(), area.top(), QStringLiteral("APPLICATIONS"), 4, QColor(16, 23, 31), area.width());
        const int top = area.top() + 70;
        const int cardW = (area.width() - 24) / 2;
        drawMetric(p, QRect(area.left(), top, cardW, 120), QStringLiteral("Browser Path"), QStringLiteral("Mesa Env"),
                   QStringLiteral("Firefox and Chromium inherit the DRM/Mesa/Vulkan environment from the ABI profile."));
        drawMetric(p, QRect(area.left() + cardW + 24, top, cardW, 120), QStringLiteral("Shell Path"), QStringLiteral("Direct KMS"),
                   QStringLiteral("This is not the old demo overlay; it is the primary desktop process."));
        drawMetric(p, QRect(area.left(), top + 142, cardW, 120), QStringLiteral("Files"), QStringLiteral("Integrated"),
                   QStringLiteral("The file surface is drawn inside the direct shell until external windows are stable."));
        drawMetric(p, QRect(area.left() + cardW + 24, top + 142, cardW, 120), QStringLiteral("Compositor"), QStringLiteral("Next"),
                   QStringLiteral("The remaining work is presenting separate app surfaces instead of internal pages."));
    }

    void drawFiles(QPainter &p, const QRect &area)
    {
        drawBitmapText(p, area.left(), area.top(), QStringLiteral("FILES"), 4, QColor(16, 23, 31), area.width());
        const QStringList rows = {
            QStringLiteral("Desktop        Folder        Ready"),
            QStringLiteral("Documents      Folder        Ready"),
            QStringLiteral("Downloads      Folder        Ready"),
            QStringLiteral("System         Volume        Mounted"),
            QStringLiteral("Applications   Catalog       Indexed")
        };
        QRect table(area.left(), area.top() + 70, area.width(), 250);
        p.setPen(QPen(QColor(198, 207, 217), 1));
        p.setBrush(QColor(255, 255, 255));
        p.drawRoundedRect(table, 7, 7);
        int y = table.top() + 34;
        drawBitmapText(p, table.left() + 18, table.top() + 16, QStringLiteral("NAME           TYPE          STATE"), 2, QColor(41, 49, 58), table.width() - 36);
        p.setPen(QColor(208, 216, 225));
        p.drawLine(table.left() + 14, y + 10, table.right() - 14, y + 10);
        y += 42;
        for (const QString &row : rows) {
            drawBitmapText(p, table.left() + 18, y, row, 2, QColor(41, 49, 58), table.width() - 36);
            y += 36;
        }
    }

    void drawMonitor(QPainter &p, const QRect &area)
    {
        drawBitmapText(p, area.left(), area.top(), QStringLiteral("MONITOR"), 4, QColor(16, 23, 31), area.width());
        const QStringList labels = {
            QStringLiteral("Frame pacing"),
            QStringLiteral("Mesa queue"),
            QStringLiteral("Wayland events"),
            QStringLiteral("Memory pressure")
        };
        int y = area.top() + 74;
        for (int i = 0; i < labels.size(); ++i) {
            QRect row(area.left(), y, area.width(), 58);
            p.setPen(QPen(QColor(198, 207, 217), 1));
            p.setBrush(QColor(255, 255, 255));
            p.drawRoundedRect(row, 7, 7);
            drawBitmapText(p, row.left() + 14, row.center().y() - 7, labels[i], 2, QColor(48, 57, 68), 140);
            QRect bar(row.left() + 170, row.center().y() - 8, row.width() - 196, 16);
            const int value = 46 + int(std::sin((phase_ * 1.5) + i * 0.9) * 28.0) + i * 5;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(230, 235, 241));
            p.drawRoundedRect(bar, 5, 5);
            p.setBrush(QColor(47, 127, 197));
            p.drawRoundedRect(QRect(bar.left(), bar.top(), qBound(8, value * bar.width() / 100, bar.width()), bar.height()), 5, 5);
            y += 72;
        }
    }

    void drawPower(QPainter &p, const QRect &area)
    {
        drawBitmapText(p, area.left(), area.top(), QStringLiteral("POWER"), 4, QColor(16, 23, 31), area.width());
        drawMetric(p, QRect(area.left(), area.top() + 74, area.width(), 120), QStringLiteral("Session"), QStringLiteral("Running"),
                   QStringLiteral("The direct Qt process is the primary desktop for this boot."));
        drawMetric(p, QRect(area.left(), area.top() + 214, area.width(), 120), QStringLiteral("Recovery"), QStringLiteral("Available"),
                   QStringLiteral("The GL compositor and native R3 shell remain packaged as fallback layers."));
    }

    void renderNow()
    {
        static int renderLogCount = 0;
        const bool logThisRender = renderLogCount++ < 4;
        if (logThisRender)
            std::fprintf(stderr, "[ridux-shell] direct render enter exposed=%d size=%dx%d\n",
                         isExposed() ? 1 : 0, width(), height());
        if (!isExposed() || width() <= 0 || height() <= 0)
            return;
        const QRegion region(0, 0, width(), height());
        store_->resize(size());
        if (logThisRender)
            std::fprintf(stderr, "[ridux-shell] direct render begin paint\n");
        store_->beginPaint(region);
        QPaintDevice *device = store_->paintDevice();
        QPainter p(device);
        p.setRenderHint(QPainter::Antialiasing, true);

        p.fillRect(0, 0, width(), height(), QColor(244, 246, 248));
        QRect panel(0, 0, width(), 58);
        p.fillRect(panel, QColor(31, 36, 44));
        drawBitmapText(p, 18, 18, QStringLiteral("RIDUXOS"), 3, QColor(247, 249, 252), 140);

        const QStringList nav = {
            QStringLiteral("Dashboard"),
            QStringLiteral("Apps"),
            QStringLiteral("Files"),
            QStringLiteral("Monitor"),
            QStringLiteral("Power")
        };
        navRects_.clear();
        int x = 165;
        for (int i = 0; i < nav.size(); ++i) {
            QRect r(x, 12, i == 0 ? 106 : 82, 34);
            navRects_.push_back(r);
            drawButton(p, r, nav[i], i == page_);
            x += r.width() + 8;
        }
        drawBitmapText(p, width() - 118, 22, QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                       2, QColor(231, 237, 244), 110);

        QRect body(34, 84, width() - 68, height() - 150);
        switch (page_) {
            case 1: drawApps(p, body); break;
            case 2: drawFiles(p, body); break;
            case 3: drawMonitor(p, body); break;
            case 4: drawPower(p, body); break;
            default: drawDashboard(p, body); break;
        }

        QRect dock(width() / 2 - 245, height() - 56, 490, 44);
        p.setPen(QPen(QColor(92, 103, 118), 1));
        p.setBrush(QColor(38, 44, 52));
        p.drawRoundedRect(dock, 8, 8);
        drawBitmapText(p, dock.left() + 22, dock.top() + 15,
                       QStringLiteral("DIRECT KMS SHELL - QT GUI + MESA + VMSVGA"),
                       2, QColor(231, 237, 244), dock.width() - 44);

        p.end();
        store_->endPaint();
        store_->flush(region, this);
        if (logThisRender)
            std::fprintf(stderr, "[ridux-shell] direct render flushed\n");
    }

    QBackingStore *store_;
    QTimer timer_;
    qreal phase_ = 0.0;
    int page_ = 0;
    QVector<QRect> navRects_;
};

class DirectKmsWindow final : public QWindow {
public:
    DirectKmsWindow()
    {
        std::fprintf(stderr, "[ridux-shell] DirectKmsWindow GL construct\n");
        QSurfaceFormat fmt;
        fmt.setRenderableType(QSurfaceFormat::OpenGLES);
        fmt.setVersion(2, 0);
        fmt.setProfile(QSurfaceFormat::NoProfile);
        fmt.setSwapInterval(1);
        setFormat(fmt);
        setTitle(QStringLiteral("RiduxOS Direct KMS GL Shell"));
        setFlags(Qt::FramelessWindowHint);
        setSurfaceType(QSurface::OpenGLSurface);
        setCursor(QCursor(Qt::ArrowCursor));
        resize(initialSize());
        QObject::connect(&timer_, &QTimer::timeout, this, [this]() {
            phase_ += 0.055;
            renderNow();
        });
        timer_.start(16);
    }

    ~DirectKmsWindow() override
    {
        if (context_ && context_->makeCurrent(this)) {
            if (gl_) {
                if (vbo_)
                    gl_->glDeleteBuffers(1, &vbo_);
                if (program_)
                    gl_->glDeleteProgram(program_);
            }
            context_->doneCurrent();
        }
    }

protected:
    void exposeEvent(QExposeEvent *) override
    {
        ensureInputGrab();
        renderNow();
    }

    void resizeEvent(QResizeEvent *) override
    {
        renderNow();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        lastPointer_ = event->pos();
        for (int i = 0; i < navRects_.size(); ++i) {
            if (navRects_[i].contains(event->pos())) {
                page_ = i;
                renderNow();
                return;
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        lastPointer_ = event->pos();
        int hover = -1;
        for (int i = 0; i < navRects_.size(); ++i) {
            if (navRects_[i].contains(event->pos())) {
                hover = i;
                break;
            }
        }
        if (hover != hoverPage_) {
            hoverPage_ = hover;
            renderNow();
        }
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        const int previous = page_;
        if (event->key() == Qt::Key_Left) {
            page_ = (page_ + 4) % 5;
        } else if (event->key() == Qt::Key_Right) {
            page_ = (page_ + 1) % 5;
        } else if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_5) {
            page_ = event->key() - Qt::Key_1;
        } else if (event->key() == Qt::Key_Escape) {
            page_ = 0;
        } else {
            QWindow::keyPressEvent(event);
            return;
        }
        if (previous != page_)
            renderNow();
        event->accept();
    }

private:
    struct Vertex {
        float x;
        float y;
        float r;
        float g;
        float b;
        float a;
    };

    static QSize initialSize()
    {
        QScreen *screen = QGuiApplication::primaryScreen();
        return screen ? screen->geometry().size() : QSize(1024, 768);
    }

    void ensureInputGrab()
    {
        if (inputGrabbed_ || !isExposed())
            return;
        inputGrabbed_ = true;
        setMouseGrabEnabled(true);
        setKeyboardGrabEnabled(true);
        std::fprintf(stderr, "[ridux-shell] direct input ready cursor=Adwaita evdev=1\n");
    }

    static void glyphRows(QChar ch, uint8_t out[7])
    {
        for (int i = 0; i < 7; ++i)
            out[i] = 0;
        switch (ch.toUpper().unicode()) {
        case 'A': { const uint8_t r[7] = {14,17,17,31,17,17,17}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'B': { const uint8_t r[7] = {30,17,17,30,17,17,30}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'C': { const uint8_t r[7] = {14,17,16,16,16,17,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'D': { const uint8_t r[7] = {30,17,17,17,17,17,30}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'E': { const uint8_t r[7] = {31,16,16,30,16,16,31}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'F': { const uint8_t r[7] = {31,16,16,30,16,16,16}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'G': { const uint8_t r[7] = {14,17,16,23,17,17,15}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'H': { const uint8_t r[7] = {17,17,17,31,17,17,17}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'I': { const uint8_t r[7] = {14,4,4,4,4,4,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'J': { const uint8_t r[7] = {7,2,2,2,18,18,12}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'K': { const uint8_t r[7] = {17,18,20,24,20,18,17}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'L': { const uint8_t r[7] = {16,16,16,16,16,16,31}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'M': { const uint8_t r[7] = {17,27,21,21,17,17,17}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'N': { const uint8_t r[7] = {17,25,21,19,17,17,17}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'O': { const uint8_t r[7] = {14,17,17,17,17,17,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'P': { const uint8_t r[7] = {30,17,17,30,16,16,16}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'Q': { const uint8_t r[7] = {14,17,17,17,21,18,13}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'R': { const uint8_t r[7] = {30,17,17,30,20,18,17}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'S': { const uint8_t r[7] = {15,16,16,14,1,1,30}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'T': { const uint8_t r[7] = {31,4,4,4,4,4,4}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'U': { const uint8_t r[7] = {17,17,17,17,17,17,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'V': { const uint8_t r[7] = {17,17,17,17,17,10,4}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'W': { const uint8_t r[7] = {17,17,17,21,21,21,10}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'X': { const uint8_t r[7] = {17,17,10,4,10,17,17}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'Y': { const uint8_t r[7] = {17,17,10,4,4,4,4}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case 'Z': { const uint8_t r[7] = {31,1,2,4,8,16,31}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '0': { const uint8_t r[7] = {14,17,19,21,25,17,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '1': { const uint8_t r[7] = {4,12,4,4,4,4,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '2': { const uint8_t r[7] = {14,17,1,2,4,8,31}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '3': { const uint8_t r[7] = {30,1,1,14,1,1,30}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '4': { const uint8_t r[7] = {2,6,10,18,31,2,2}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '5': { const uint8_t r[7] = {31,16,16,30,1,1,30}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '6': { const uint8_t r[7] = {6,8,16,30,17,17,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '7': { const uint8_t r[7] = {31,1,2,4,8,8,8}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '8': { const uint8_t r[7] = {14,17,17,14,17,17,14}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '9': { const uint8_t r[7] = {14,17,17,15,1,2,12}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case ':': { const uint8_t r[7] = {0,4,4,0,4,4,0}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '/': { const uint8_t r[7] = {1,1,2,4,8,16,16}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '-': { const uint8_t r[7] = {0,0,0,14,0,0,0}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '.': { const uint8_t r[7] = {0,0,0,0,0,12,12}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        case '+': { const uint8_t r[7] = {0,4,4,31,4,4,0}; for (int i=0;i<7;++i) out[i]=r[i]; break; }
        default: break;
        }
    }

    GLuint compileShader(GLenum type, const char *source)
    {
        GLuint shader = gl_->glCreateShader(type);
        if (!shader) {
            std::fprintf(stderr, "[ridux-shell] glCreateShader failed type=%u\n", unsigned(type));
            return 0;
        }
        gl_->glShaderSource(shader, 1, &source, nullptr);
        gl_->glCompileShader(shader);
        GLint ok = 0;
        gl_->glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            GLsizei len = 0;
            log[0] = 0;
            gl_->glGetShaderInfoLog(shader, GLsizei(sizeof(log) - 1), &len, log);
            log[(len >= 0 && len < GLsizei(sizeof(log))) ? len : GLsizei(sizeof(log) - 1)] = 0;
            std::fprintf(stderr, "[ridux-shell] shader compile failed type=%u log=%s\n",
                         unsigned(type), log);
            gl_->glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    bool initGl()
    {
        if (glReady_)
            return true;
        if (!context_) {
            context_ = new QOpenGLContext(this);
            context_->setFormat(format());
            if (!context_->create()) {
                std::fprintf(stderr, "[ridux-shell] GL context create failed\n");
                return false;
            }
        }
        if (!context_->makeCurrent(this)) {
            std::fprintf(stderr, "[ridux-shell] GL makeCurrent failed\n");
            return false;
        }
        gl_ = context_->functions();
        if (!glFunctionsReady_) {
            gl_->initializeOpenGLFunctions();
            glFunctionsReady_ = true;
        }
        const GLubyte *vendor = gl_->glGetString(GL_VENDOR);
        const GLubyte *renderer = gl_->glGetString(GL_RENDERER);
        const GLubyte *version = gl_->glGetString(GL_VERSION);
        const GLubyte *shading = gl_->glGetString(GL_SHADING_LANGUAGE_VERSION);
        std::fprintf(stderr, "[ridux-shell] GL info vendor=%s renderer=%s version=%s glsl=%s\n",
                     vendor ? reinterpret_cast<const char *>(vendor) : "?",
                     renderer ? reinterpret_cast<const char *>(renderer) : "?",
                     version ? reinterpret_cast<const char *>(version) : "?",
                     shading ? reinterpret_cast<const char *>(shading) : "?");
        const QByteArray vendorName = vendor ? QByteArray(reinterpret_cast<const char *>(vendor)) : QByteArray();
        const QByteArray rendererName = renderer ? QByteArray(reinterpret_cast<const char *>(renderer)) : QByteArray();
        const QByteArray versionName = version ? QByteArray(reinterpret_cast<const char *>(version)) : QByteArray();
        const QByteArray rendererProbe = vendorName + QByteArray(" ") + rendererName + QByteArray(" ") + versionName;
        if (envTruthy("RIDUX_REQUIRE_HARDWARE_GL")) {
            if (rendererName.isEmpty() || rendererLooksSoftware(rendererProbe) ||
                !rendererLooksHardwareMesa(rendererProbe)) {
                std::fprintf(stderr,
                             "[ridux-shell] rejected software/unknown GL renderer vendor=%s renderer=%s version=%s\n",
                             vendorName.constData(), rendererName.constData(), versionName.constData());
                context_->doneCurrent();
                return false;
            }
            std::fprintf(stderr, "[ridux-mesa-real] renderer=%s status=hardware-required accepted\n",
                         rendererName.constData());
        }
        const bool glesContext = containsCi(versionName, "OpenGL ES");
        const char *vs = glesContext ?
            "#version 100\n"
            "attribute vec2 a_pos;\n"
            "attribute vec4 a_color;\n"
            "varying vec4 v_color;\n"
            "void main(){ gl_Position=vec4(a_pos,0.0,1.0); v_color=a_color; }\n" :
            "attribute vec2 a_pos;\n"
            "attribute vec4 a_color;\n"
            "varying vec4 v_color;\n"
            "void main(){ gl_Position=vec4(a_pos,0.0,1.0); v_color=a_color; }\n";
        const char *fs = glesContext ?
            "#version 100\n"
            "precision mediump float;\n"
            "varying vec4 v_color;\n"
            "void main(){ gl_FragColor=v_color; }\n" :
            "#ifdef GL_ES\n"
            "precision mediump float;\n"
            "#endif\n"
            "varying vec4 v_color;\n"
            "void main(){ gl_FragColor=v_color; }\n";
        GLuint vertex = compileShader(GL_VERTEX_SHADER, vs);
        GLuint frag = compileShader(GL_FRAGMENT_SHADER, fs);
        if (vertex && frag) {
            program_ = gl_->glCreateProgram();
            if (program_) {
                gl_->glAttachShader(program_, vertex);
                gl_->glAttachShader(program_, frag);
                gl_->glLinkProgram(program_);
                GLint linked = 0;
                gl_->glGetProgramiv(program_, GL_LINK_STATUS, &linked);
                if (!linked) {
                    char log[512];
                    GLsizei len = 0;
                    log[0] = 0;
                    gl_->glGetProgramInfoLog(program_, GLsizei(sizeof(log) - 1), &len, log);
                    log[(len >= 0 && len < GLsizei(sizeof(log))) ? len : GLsizei(sizeof(log) - 1)] = 0;
                    std::fprintf(stderr, "[ridux-shell] shader program link failed log=%s\n", log);
                    gl_->glDeleteProgram(program_);
                    program_ = 0;
                }
            }
        }
        if (vertex)
            gl_->glDeleteShader(vertex);
        if (frag)
            gl_->glDeleteShader(frag);
        if (program_) {
            posAttr_ = gl_->glGetAttribLocation(program_, "a_pos");
            colorAttr_ = gl_->glGetAttribLocation(program_, "a_color");
            if (posAttr_ < 0 || colorAttr_ < 0) {
                std::fprintf(stderr, "[ridux-shell] shader attributes missing\n");
                gl_->glDeleteProgram(program_);
                program_ = 0;
                posAttr_ = -1;
                colorAttr_ = -1;
            }
        }
        gl_->glGenBuffers(1, &vbo_);
        if (!program_ || !vbo_) {
            std::fprintf(stderr, "[ridux-shell] GL shader/VBO path unavailable; refusing non-shader fallback\n");
            context_->doneCurrent();
            return false;
        }
        gl_->glEnable(GL_BLEND);
        gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        std::fprintf(stderr, "[ridux-shell] GL renderer ready mode=shader-vbo\n");
        glReady_ = true;
        return true;
    }

    void addVertex(float x, float y, const QColor &c)
    {
        vertices_.push_back(Vertex{x, y, float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF())});
    }

    void addRect(int x, int y, int w, int h, const QColor &c)
    {
        if (w <= 0 || h <= 0 || width() <= 0 || height() <= 0)
            return;
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > width()) w = width() - x;
        if (y + h > height()) h = height() - y;
        if (w <= 0 || h <= 0)
            return;
        const float x0 = float(x) * 2.0f / float(width()) - 1.0f;
        const float x1 = float(x + w) * 2.0f / float(width()) - 1.0f;
        const float y0 = 1.0f - float(y) * 2.0f / float(height());
        const float y1 = 1.0f - float(y + h) * 2.0f / float(height());
        addVertex(x0, y0, c); addVertex(x1, y0, c); addVertex(x1, y1, c);
        addVertex(x0, y0, c); addVertex(x1, y1, c); addVertex(x0, y1, c);
    }

    void addOutline(const QRect &r, const QColor &c)
    {
        addRect(r.left(), r.top(), r.width(), 1, c);
        addRect(r.left(), r.bottom(), r.width(), 1, c);
        addRect(r.left(), r.top(), 1, r.height(), c);
        addRect(r.right(), r.top(), 1, r.height(), c);
    }

    void drawBitmapText(int x, int y, const QString &text, int scale, const QColor &color, int maxWidth = 0)
    {
        int cx = x;
        const int limit = maxWidth > 0 ? x + maxWidth : 0x3fffffff;
        for (QChar ch : text) {
            uint8_t rows[7];
            if (ch == '\n') {
                cx = x;
                y += 9 * scale;
                continue;
            }
            if (ch.isSpace()) {
                cx += 4 * scale;
                continue;
            }
            if (cx + 5 * scale > limit)
                break;
            glyphRows(ch, rows);
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 5; ++col) {
                    if (rows[row] & (1u << (4 - col)))
                        addRect(cx + col * scale, y + row * scale, scale, scale, color);
                }
            }
            cx += 6 * scale;
        }
    }

    void drawButton(const QRect &r, const QString &text, bool active, bool hover = false)
    {
        addRect(r.left(), r.top(), r.width(), r.height(),
                active ? QColor(38, 116, 172) : (hover ? QColor(57, 68, 80) : QColor(45, 52, 61)));
        addOutline(r, active ? QColor(137, 190, 235) : (hover ? QColor(119, 136, 154) : QColor(87, 98, 112)));
        const int tw = text.size() * 12;
        drawBitmapText(r.center().x() - tw / 2, r.center().y() - 7, text, 2, QColor(245, 248, 252), r.width() - 10);
    }

    void drawCard(const QRect &r)
    {
        addRect(r.left(), r.top(), r.width(), r.height(), QColor(255, 255, 255));
        addOutline(r, QColor(198, 207, 217));
    }

    void drawMetric(const QRect &r, const QString &label, const QString &value, const QString &detail)
    {
        drawCard(r);
        drawBitmapText(r.left() + 14, r.top() + 12, label, 2, QColor(74, 84, 96), r.width() - 28);
        drawBitmapText(r.left() + 14, r.top() + 38, value, 4, QColor(15, 22, 30), r.width() - 28);
        drawBitmapText(r.left() + 14, r.top() + 84, detail, 2, QColor(91, 101, 113), r.width() - 28);
    }

    void drawPulse(const QRect &r)
    {
        addRect(r.left(), r.top(), r.width(), r.height(), QColor(237, 243, 248));
        for (int x = r.left() + 24; x < r.right(); x += 44)
            addRect(x, r.top(), 1, r.height(), QColor(182, 194, 207, 90));
        for (int y = r.top() + 24; y < r.bottom(); y += 44)
            addRect(r.left(), y, r.width(), 1, QColor(182, 194, 207, 90));
        const QColor colors[] = {
            QColor(47, 127, 197, 185),
            QColor(38, 156, 119, 170),
            QColor(217, 143, 49, 160),
            QColor(178, 80, 124, 148)
        };
        for (int i = 0; i < 14; ++i) {
            const double t = phase_ + i * 0.48;
            const int sz = 24 + int(8.0 * std::sin(t * 1.35));
            const int cx = r.center().x() + int(std::cos(t) * (r.width() * 0.24));
            const int cy = r.center().y() + int(std::sin(t * 0.82) * (r.height() * 0.20));
            addRect(cx - sz / 2, cy - sz / 2, sz, sz, colors[i % 4]);
        }
    }

    void drawDashboard(const QRect &area)
    {
        drawBitmapText(area.left() + 4, area.top(), QStringLiteral("RIDUXOS DIRECT GPU SHELL"), 4, QColor(16, 23, 31), area.width() - 8);
        drawBitmapText(area.left() + 5, area.top() + 42,
                       QStringLiteral("QT OPENGL FULLSCREEN ON EGLFS MESA GBM KMS /DEV/DRI/CARD0"),
                       2, QColor(86, 96, 108), area.width() - 10);
        const int cardW = (area.width() - 24) / 2;
        const int top = area.top() + 78;
        drawMetric(QRect(area.left(), top, cardW, 112), QStringLiteral("Display"), QStringLiteral("KMS"),
                   QStringLiteral("Primary scanout is owned by the DRM node."));
        drawMetric(QRect(area.left() + cardW + 24, top, cardW, 112), QStringLiteral("Renderer"), QStringLiteral("Mesa"),
                   QStringLiteral("VirtualBox VMSVGA 3D command path is active."));
        drawMetric(QRect(area.left(), top + 130, cardW, 112), QStringLiteral("Runtime"), QStringLiteral("Qt GL"),
                   QStringLiteral("QWindow and OpenGL avoid the Freetype paint path."));
        drawMetric(QRect(area.left() + cardW + 24, top + 130, cardW, 112), QStringLiteral("Events"), QStringLiteral("eventfd"),
                   QStringLiteral("poll eventfd and QThread are live in the ABI."));
        QRect pulse(area.left(), top + 266, area.width(), qMax(160, area.bottom() - top - 276));
        drawCard(pulse);
        drawPulse(pulse.adjusted(1, 1, -1, -1));
        drawBitmapText(pulse.left() + 16, pulse.top() + 14, QStringLiteral("LIVE GPU SURFACE"), 2, QColor(20, 26, 34), pulse.width() - 32);
    }

    void drawApps(const QRect &area)
    {
        drawBitmapText(area.left(), area.top(), QStringLiteral("APPLICATIONS"), 4, QColor(16, 23, 31), area.width());
        const int cardW = (area.width() - 24) / 2;
        const int top = area.top() + 70;
        drawMetric(QRect(area.left(), top, cardW, 120), QStringLiteral("Browser"), QStringLiteral("Mesa Env"),
                   QStringLiteral("Apps inherit DRM Mesa Vulkan variables."));
        drawMetric(QRect(area.left() + cardW + 24, top, cardW, 120), QStringLiteral("Shell"), QStringLiteral("Direct KMS"),
                   QStringLiteral("This process is the primary desktop."));
        drawMetric(QRect(area.left(), top + 142, cardW, 120), QStringLiteral("Files"), QStringLiteral("Integrated"),
                   QStringLiteral("Internal file surface stays available."));
        drawMetric(QRect(area.left() + cardW + 24, top + 142, cardW, 120), QStringLiteral("Compositor"), QStringLiteral("Next"),
                   QStringLiteral("Separate app surfaces come after this pass."));
    }

    void drawFiles(const QRect &area)
    {
        drawBitmapText(area.left(), area.top(), QStringLiteral("FILES"), 4, QColor(16, 23, 31), area.width());
        QRect table(area.left(), area.top() + 70, area.width(), 250);
        drawCard(table);
        drawBitmapText(table.left() + 18, table.top() + 16, QStringLiteral("NAME           TYPE          STATE"), 2, QColor(41, 49, 58), table.width() - 36);
        addRect(table.left() + 14, table.top() + 44, table.width() - 28, 1, QColor(208, 216, 225));
        const QStringList rows = {
            QStringLiteral("Desktop        Folder        Ready"),
            QStringLiteral("Documents      Folder        Ready"),
            QStringLiteral("Downloads      Folder        Ready"),
            QStringLiteral("System         Volume        Mounted"),
            QStringLiteral("Applications   Catalog       Indexed")
        };
        int y = table.top() + 74;
        for (const QString &row : rows) {
            drawBitmapText(table.left() + 18, y, row, 2, QColor(41, 49, 58), table.width() - 36);
            y += 36;
        }
    }

    void drawMonitor(const QRect &area)
    {
        drawBitmapText(area.left(), area.top(), QStringLiteral("MONITOR"), 4, QColor(16, 23, 31), area.width());
        const QStringList labels = {
            QStringLiteral("Frame pacing"),
            QStringLiteral("Mesa queue"),
            QStringLiteral("DRM events"),
            QStringLiteral("Memory pressure")
        };
        int y = area.top() + 74;
        for (int i = 0; i < labels.size(); ++i) {
            QRect row(area.left(), y, area.width(), 58);
            drawCard(row);
            drawBitmapText(row.left() + 14, row.center().y() - 7, labels[i], 2, QColor(48, 57, 68), 150);
            QRect bar(row.left() + 180, row.center().y() - 8, row.width() - 206, 16);
            const int value = 46 + int(std::sin((phase_ * 1.5) + i * 0.9) * 28.0) + i * 5;
            addRect(bar.left(), bar.top(), bar.width(), bar.height(), QColor(230, 235, 241));
            addRect(bar.left(), bar.top(), qBound(8, value * bar.width() / 100, bar.width()), bar.height(), QColor(47, 127, 197));
            y += 72;
        }
    }

    void drawPower(const QRect &area)
    {
        drawBitmapText(area.left(), area.top(), QStringLiteral("POWER"), 4, QColor(16, 23, 31), area.width());
        drawMetric(QRect(area.left(), area.top() + 74, area.width(), 120), QStringLiteral("Session"), QStringLiteral("Running"),
                   QStringLiteral("The direct Qt GL shell owns this boot."));
        drawMetric(QRect(area.left(), area.top() + 214, area.width(), 120), QStringLiteral("Recovery"), QStringLiteral("Available"),
                   QStringLiteral("GL compositor and native R3 shell stay packaged."));
    }

    void buildUi()
    {
        vertices_.clear();
        addRect(0, 0, width(), height(), QColor(244, 246, 248));
        addRect(0, 0, width(), 58, QColor(31, 36, 44));
        drawBitmapText(18, 18, QStringLiteral("RIDUXOS"), 3, QColor(247, 249, 252), 140);
        const QStringList nav = {
            QStringLiteral("Dashboard"),
            QStringLiteral("Apps"),
            QStringLiteral("Files"),
            QStringLiteral("Monitor"),
            QStringLiteral("Power")
        };
        navRects_.clear();
        int x = 165;
        for (int i = 0; i < nav.size(); ++i) {
            QRect r(x, 12, i == 0 ? 106 : 82, 34);
            navRects_.push_back(r);
            drawButton(r, nav[i], i == page_, i == hoverPage_);
            x += r.width() + 8;
        }
        drawBitmapText(width() - 118, 22, QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                       2, QColor(231, 237, 244), 110);
        QRect body(34, 84, width() - 68, height() - 150);
        switch (page_) {
            case 1: drawApps(body); break;
            case 2: drawFiles(body); break;
            case 3: drawMonitor(body); break;
            case 4: drawPower(body); break;
            default: drawDashboard(body); break;
        }
        QRect dock(width() / 2 - 245, height() - 56, 490, 44);
        addRect(dock.left(), dock.top(), dock.width(), dock.height(), QColor(38, 44, 52));
        addOutline(dock, QColor(92, 103, 118));
        drawBitmapText(dock.left() + 22, dock.top() + 15,
                       QStringLiteral("DIRECT KMS SHELL - QT OPENGL + MESA + VMSVGA"),
                       2, QColor(231, 237, 244), dock.width() - 44);
    }

    void renderNow()
    {
        static int renderLogCount = 0;
        const bool logThisRender = renderLogCount++ < 8;
        if (logThisRender)
            std::fprintf(stderr, "[ridux-shell] direct GL render enter exposed=%d size=%dx%d\n",
                         isExposed() ? 1 : 0, width(), height());
        if (!isExposed() || width() <= 0 || height() <= 0)
            return;
        if (!initGl())
            return;
        if (!context_->makeCurrent(this)) {
            std::fprintf(stderr, "[ridux-shell] GL makeCurrent render failed\n");
            return;
        }
        buildUi();
        gl_->glViewport(0, 0, width(), height());
        gl_->glClearColor(0.957f, 0.965f, 0.973f, 1.0f);
        gl_->glClear(GL_COLOR_BUFFER_BIT);
        gl_->glUseProgram(program_);
        gl_->glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl_->glBufferData(GL_ARRAY_BUFFER,
                          GLsizeiptr(vertices_.size() * int(sizeof(Vertex))),
                          vertices_.constData(),
                          GL_DYNAMIC_DRAW);
        gl_->glEnableVertexAttribArray(GLuint(posAttr_));
        gl_->glVertexAttribPointer(GLuint(posAttr_), 2, GL_FLOAT, GL_FALSE,
                                   sizeof(Vertex), reinterpret_cast<void *>(0));
        gl_->glEnableVertexAttribArray(GLuint(colorAttr_));
        gl_->glVertexAttribPointer(GLuint(colorAttr_), 4, GL_FLOAT, GL_FALSE,
                                   sizeof(Vertex), reinterpret_cast<void *>(sizeof(float) * 2));
        gl_->glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices_.size()));
        gl_->glDisableVertexAttribArray(GLuint(posAttr_));
        gl_->glDisableVertexAttribArray(GLuint(colorAttr_));
        context_->swapBuffers(this);
        if (logThisRender)
            std::fprintf(stderr, "[ridux-shell] direct GL render swapped vertices=%d\n", int(vertices_.size()));
    }

    QOpenGLContext *context_ = nullptr;
    QOpenGLFunctions *gl_ = nullptr;
    bool glFunctionsReady_ = false;
    bool glReady_ = false;
    QTimer timer_;
    QVector<Vertex> vertices_;
    GLuint program_ = 0;
    GLuint vbo_ = 0;
    GLint posAttr_ = -1;
    GLint colorAttr_ = -1;
    qreal phase_ = 0.0;
    int page_ = 0;
    int hoverPage_ = -1;
    QPoint lastPointer_;
    bool inputGrabbed_ = false;
    QVector<QRect> navRects_;
};

static QPushButton *button(const QString &text, const QString &program, const char *objectName = nullptr)
{
    auto *btn = new QPushButton(text);
    if (objectName)
        btn->setObjectName(objectName);
    QObject::connect(btn, &QPushButton::clicked, btn, [program]() { startProgram(program); });
    return btn;
}

class PulseSurface final : public QWidget {
public:
    explicit PulseSurface(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(360, 190);
        setAutoFillBackground(false);
        connect(&timer_, &QTimer::timeout, this, [this]() {
            phase_ += 0.05;
            update();
        });
        timer_.start(16);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = rect().adjusted(1, 1, -1, -1);
        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0.0, QColor(247, 249, 251));
        bg.setColorAt(1.0, QColor(216, 224, 232));
        p.fillRect(r, bg);

        p.setPen(QPen(QColor(166, 176, 188, 88), 1));
        for (int x = 20; x < width(); x += 40)
            p.drawLine(x, 0, x, height());
        for (int y = 20; y < height(); y += 40)
            p.drawLine(0, y, width(), y);

        const QPointF center(width() * 0.5, height() * 0.54);
        const QColor colors[] = {
            QColor(47, 127, 197, 190),
            QColor(38, 156, 119, 175),
            QColor(217, 143, 49, 168),
            QColor(178, 80, 124, 152)
        };
        for (int i = 0; i < 6; ++i) {
            const qreal t = phase_ + i * 0.72;
            const qreal radius = 28 + 12 * std::sin(t * 1.4);
            const QPointF c(center.x() + std::cos(t) * 98,
                            center.y() + std::sin(t * 0.82) * 42);
            QColor color = colors[i % 4];
            p.setPen(QPen(color.darker(122), 1));
            p.setBrush(color);
            p.drawEllipse(c, radius, radius);
        }

        QPainterPath wave;
        wave.moveTo(24, height() - 42);
        for (int x = 24; x < width() - 24; x += 12) {
            const qreal y = height() - 52 - std::sin(phase_ * 2.2 + x * 0.035) * 20;
            wave.lineTo(x, y);
        }
        p.setPen(QPen(QColor(31, 86, 132), 3));
        p.drawPath(wave);

        p.setPen(QColor(24, 28, 34));
        p.setFont(QFont("DejaVu Sans", 10, QFont::DemiBold));
        p.drawText(QRectF(16, 12, width() - 32, 28),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   "Mesa accelerated Qt surface");
    }

private:
    QTimer timer_;
    qreal phase_ = 0.0;
};

static QFrame *metricCard(const QString &title, const QString &value, const QString &detail)
{
    auto *frame = new QFrame;
    frame->setObjectName("surface");
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(5);

    auto *name = new QLabel(title);
    if (!g_directKmsShell)
        name->setStyleSheet("font-weight: 800; color: #333b46;");
    auto *main = new QLabel(value);
    if (!g_directKmsShell)
        main->setStyleSheet("font-size: 21px; font-weight: 900; color: #111820;");
    auto *sub = new QLabel(detail);
    sub->setObjectName("subtitle");
    sub->setWordWrap(true);

    layout->addWidget(name);
    layout->addWidget(main);
    layout->addWidget(sub);
    return frame;
}

static QMainWindow *makePanel()
{
    const QRect screen = primaryGeometry();
    auto *win = new QMainWindow;
    win->setWindowTitle("Ridux Panel");
    win->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    auto *root = new QFrame;
    root->setObjectName("panelRoot");
    auto *layout = new QHBoxLayout(root);
    layout->setContentsMargins(12, 5, 12, 5);
    layout->setSpacing(8);

    auto *brand = new QLabel("RiduxOS");
    brand->setObjectName("brand");
    layout->addWidget(brand);
    layout->addSpacing(8);
    layout->addWidget(button("Apps", "/usr/bin/ridux-open-launcher", "panelButton"));
    layout->addWidget(button("Files", "/usr/bin/ridux-open-files", "panelButton"));
    layout->addWidget(button("Terminal", "/usr/bin/ridux-terminal", "panelButton"));
    layout->addWidget(button("Displays", "/usr/bin/ridux-display-settings", "panelButton"));
    layout->addStretch(1);

    auto *clock = new QLabel;
    clock->setObjectName("clock");
    auto *timer = new QTimer(win);
    QObject::connect(timer, &QTimer::timeout, win, [clock]() {
        clock->setText(QDateTime::currentDateTime().toString("HH:mm"));
    });
    timer->start(1000);
    clock->setText(QDateTime::currentDateTime().toString("HH:mm"));
    layout->addWidget(clock);
    layout->addWidget(button("Power", "/usr/bin/ridux-power-menu", "panelButton"));

    win->setCentralWidget(root);
    const int width = qBound(720, screen.width() - 72, 1080);
    win->resize(width, 48);
    win->move(screen.x() + (screen.width() - width) / 2, screen.y() + 18);
    return win;
}

static QMainWindow *makeDock()
{
    const QRect screen = primaryGeometry();
    auto *win = new QMainWindow;
    win->setWindowTitle("Ridux Dock");
    win->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    auto *root = new QFrame;
    root->setObjectName("dockRoot");
    auto *layout = new QHBoxLayout(root);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);
    layout->addWidget(button("Apps", "/usr/bin/ridux-open-launcher", "dockButton"));
    layout->addWidget(button("Files", "/usr/bin/ridux-open-files", "dockButton"));
    layout->addWidget(button("Terminal", "/usr/bin/ridux-terminal", "dockButton"));
    layout->addWidget(button("Monitor", "/usr/bin/ridux-monitor-qt", "dockButton"));
    layout->addWidget(button("Power", "/usr/bin/ridux-power-menu", "dockButton"));

    win->setCentralWidget(root);
    const int width = qBound(500, screen.width() - 96, 720);
    win->resize(width, 62);
    win->move(screen.x() + (screen.width() - width) / 2,
              screen.y() + qMax(0, screen.height() - 86));
    return win;
}

static QMainWindow *makeDashboard()
{
    auto *win = new QMainWindow;
    win->setWindowTitle("Ridux Control Center");
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel("Ridux Control Center");
    title->setObjectName("title");
    auto *subtitle = new QLabel("Native Ridux compositor, Qt shell surfaces, Mesa/OpenGL application path");
    subtitle->setObjectName("subtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);

    auto *grid = new QGridLayout;
    grid->setSpacing(10);
    grid->addWidget(metricCard("Compositor", "Ridux", "Kernel-owned window manager and Wayland surface bridge"), 0, 0);
    grid->addWidget(metricCard("Renderer", "Mesa GL", "Qt and GTK clients use the Ridux DRM/Mesa path"), 0, 1);
    grid->addWidget(metricCard("Shell", "Qt 6", "Panel, dock and control surfaces are Ridux Qt widgets"), 1, 0);
    grid->addWidget(metricCard("Build", "Fast ISO", "KDE and Wayfire are excluded from the primary image"), 1, 1);
    layout->addLayout(grid);

    layout->addWidget(new PulseSurface);

    auto *controls = new QGridLayout;
    controls->setSpacing(8);
    controls->addWidget(button("Launcher", "/usr/bin/ridux-open-launcher"), 0, 0);
    controls->addWidget(button("Files", "/usr/bin/ridux-open-files"), 0, 1);
    controls->addWidget(button("Terminal", "/usr/bin/ridux-terminal"), 0, 2);
    controls->addWidget(button("Displays", "/usr/bin/ridux-display-settings"), 1, 0);
    controls->addWidget(button("Screenshot", "/usr/bin/ridux-screenshot"), 1, 1);
    controls->addWidget(button("Power", "/usr/bin/ridux-power-menu"), 1, 2);
    layout->addLayout(controls);

    win->setCentralWidget(root);
    win->resize(700, 570);
    return win;
}

static QMainWindow *makeFiles()
{
    auto *win = new QMainWindow;
    win->setWindowTitle("Ridux Files");
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel("Ridux Files");
    title->setObjectName("title");
    auto *subtitle = new QLabel("Local volumes, user folders and system locations");
    subtitle->setObjectName("subtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);

    auto *table = new QTableWidget(8, 4);
    table->setHorizontalHeaderLabels({"Name", "Type", "Size", "Modified"});
    const QStringList names = {
        "Desktop", "Documents", "Downloads", "Pictures",
        "Music", "Videos", "System", "Applications"
    };
    const QStringList types = {
        "Folder", "Folder", "Folder", "Folder",
        "Folder", "Folder", "Volume", "Catalog"
    };
    for (int i = 0; i < names.size(); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(names[i]));
        table->setItem(i, 1, new QTableWidgetItem(types[i]));
        table->setItem(i, 2, new QTableWidgetItem(i < 6 ? "-" : "Ready"));
        table->setItem(i, 3, new QTableWidgetItem(QDateTime::currentDateTime().addSecs(-i * 420).toString("HH:mm:ss")));
    }
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    layout->addWidget(table);

    auto *row = new QHBoxLayout;
    row->addWidget(new QPushButton("New Folder"));
    row->addWidget(new QPushButton("Copy"));
    row->addWidget(new QPushButton("Properties"));
    row->addStretch(1);
    layout->addLayout(row);

    win->setCentralWidget(root);
    win->resize(640, 460);
    return win;
}

static QMainWindow *makeMonitor()
{
    auto *win = new QMainWindow;
    win->setWindowTitle("Ridux Monitor");
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel("Ridux Monitor");
    title->setObjectName("title");
    auto *subtitle = new QLabel("Frame pacing, input and compositor activity");
    subtitle->setObjectName("subtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);

    QList<QProgressBar *> bars;
    const QStringList labels = {"Frame pacing", "Mesa queue", "Wayland events", "Memory pressure"};
    for (const QString &label : labels) {
        auto *frame = new QFrame;
        frame->setObjectName("surface");
        auto *row = new QHBoxLayout(frame);
        row->setContentsMargins(12, 10, 12, 10);
        auto *name = new QLabel(label);
        name->setMinimumWidth(120);
        auto *bar = new QProgressBar;
        bar->setRange(0, 100);
        row->addWidget(name);
        row->addWidget(bar, 1);
        layout->addWidget(frame);
        bars.push_back(bar);
    }

    auto *events = new QListWidget;
    events->addItems({
        "Ridux compositor active",
        "Wayland bridge listening on wayland-0",
        "Qt shell running as Ridux session",
        "Mesa/OpenGL client path enabled"
    });
    layout->addWidget(events, 1);

    auto *timer = new QTimer(win);
    QObject::connect(timer, &QTimer::timeout, win, [bars, events]() {
        static int tick = 0;
        ++tick;
        for (int i = 0; i < bars.size(); ++i) {
            const int value = 50 + int(std::sin((tick + i * 21) * 0.085) * 26.0) + i * 4;
            bars[i]->setValue(qBound(0, value, 100));
        }
        if (tick % 90 == 0) {
            events->insertItem(0, QDateTime::currentDateTime().toString("HH:mm:ss") + " compositor frame");
            while (events->count() > 8)
                delete events->takeItem(events->count() - 1);
        }
    });
    timer->start(33);

    win->setCentralWidget(root);
    win->resize(540, 440);
    return win;
}

static QPushButton *navButton(const QString &text, QStackedWidget *stack, int page, const char *objectName)
{
    auto *btn = new QPushButton(text);
    if (objectName)
        btn->setObjectName(objectName);
    QObject::connect(btn, &QPushButton::clicked, btn, [stack, page]() {
        if (stack && page >= 0 && page < stack->count())
            stack->setCurrentIndex(page);
    });
    return btn;
}

class MesaProbeWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit MesaProbeWidget(QWidget *parent = nullptr)
        : QOpenGLWidget(parent)
    {
        setObjectName("surface");
        setMinimumHeight(126);
        auto *timer = new QTimer(this);
        QObject::connect(timer, &QTimer::timeout, this, [this]() { update(); });
        timer->start(16);
    }

protected:
    void initializeGL() override
    {
        initializeOpenGLFunctions();
        if (!logCurrentHardwareRenderer("qtwidgets-opengl"))
            QCoreApplication::exit(134);
    }

    void resizeGL(int w, int h) override
    {
        glViewport(0, 0, w, h);
    }

    void paintGL() override
    {
        ++frame_;
        const float pulse = 0.5f + 0.5f * std::sin(frame_ * 0.045f);
        glClearColor(0.08f + pulse * 0.06f, 0.13f + pulse * 0.04f, 0.18f + pulse * 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

private:
    int frame_ = 0;
};

static QWidget *makeDirectDashboardPage()
{
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel("RiduxOS Direct GPU Shell");
    title->setObjectName("title");
    auto *subtitle = new QLabel("Qt 6 fullscreen session on EGLFS, Mesa GBM and KMS /dev/dri/card0");
    subtitle->setObjectName("subtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);

    auto *grid = new QGridLayout;
    grid->setSpacing(10);
    grid->addWidget(metricCard("Display", "KMS", "Qt owns the primary scanout through the Ridux DRM node"), 0, 0);
    grid->addWidget(metricCard("Renderer", "Mesa", "The fullscreen Qt shell is presented through EGLFS/GBM"), 0, 1);
    grid->addWidget(metricCard("Mode", "EGLFS", "No demo overlay and no fake top-level compositor in front"), 1, 0);
    grid->addWidget(metricCard("Target", "VirtualBox", "Mesa selects the VMSVGA/SVGA3D path when available"), 1, 1);
    layout->addLayout(grid);
    layout->addWidget(new MesaProbeWidget, 1);
    return root;
}

static QWidget *makeDirectLauncherPage(QStackedWidget *stack)
{
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel("Applications");
    title->setObjectName("title");
    auto *subtitle = new QLabel("Integrated shell surfaces for the direct KMS session");
    subtitle->setObjectName("subtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);

    auto *grid = new QGridLayout;
    grid->setSpacing(10);
    grid->addWidget(navButton("Dashboard", stack, 0, nullptr), 0, 0);
    grid->addWidget(navButton("Files", stack, 2, nullptr), 0, 1);
    grid->addWidget(navButton("Monitor", stack, 3, nullptr), 0, 2);
    grid->addWidget(navButton("Power", stack, 4, nullptr), 0, 3);
    grid->addWidget(metricCard("Browser path", "Ready", "Firefox/Chromium keep the Mesa/Vulkan env in the Linux ABI layer"), 1, 0, 1, 2);
    grid->addWidget(metricCard("Window path", "Next", "External app compositing is kept behind the direct shell session"), 1, 2, 1, 2);
    layout->addLayout(grid);
    layout->addStretch(1);
    return root;
}

static QWidget *makeDirectPowerPage()
{
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel("Power");
    title->setObjectName("title");
    auto *subtitle = new QLabel("Session controls for the direct KMS shell");
    subtitle->setObjectName("subtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(metricCard("Session", "Running", "Qt shell owns the display; fallback compositor remains packaged"));
    layout->addWidget(metricCard("Recovery", "Available", "The native R3 desktop is still present as a boot fallback"));
    layout->addStretch(1);
    return root;
}

static QWidget *makeDirectFilesPage()
{
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel("Files");
    title->setObjectName("title");
    auto *subtitle = new QLabel("Direct desktop file targets");
    subtitle->setObjectName("subtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(metricCard("Desktop", "Ready", "Interactive target in the Qt shell"));
    layout->addWidget(metricCard("Documents", "Ready", "User documents namespace mounted"));
    layout->addWidget(metricCard("System", "Ready", "Root filesystem visible through Ridux VFS"));
    layout->addStretch(1);
    return root;
}

static QWidget *makeDirectMonitorPage()
{
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel("Monitor");
    title->setObjectName("title");
    auto *subtitle = new QLabel("GPU runtime health");
    subtitle->setObjectName("subtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(metricCard("Frame pacing", "Active", "Qt event loop is ticking with vsync requested"));
    layout->addWidget(metricCard("DRM events", "Active", "KMS cursor and eventfd wakeups are flowing"));
    layout->addWidget(metricCard("Mesa probe", "Required", "The OpenGL widget exits if Mesa resolves to software"));
    layout->addStretch(1);
    return root;
}

static QMainWindow *makeDesktopSession()
{
    auto *win = new QMainWindow;
    win->setWindowTitle("RiduxOS Qt KMS Shell");
    win->setWindowFlags(Qt::FramelessWindowHint);

    auto *root = new QFrame;
    root->setObjectName("launcherRoot");
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(10);

    auto *stack = new QStackedWidget;
    stack->addWidget(makeDirectDashboardPage());
    stack->addWidget(makeDirectLauncherPage(stack));
    stack->addWidget(makeDirectFilesPage());
    stack->addWidget(makeDirectMonitorPage());
    stack->addWidget(makeDirectPowerPage());

    auto *panel = new QFrame;
    panel->setObjectName("panelRoot");
    auto *panelLayout = new QHBoxLayout(panel);
    panelLayout->setContentsMargins(12, 5, 12, 5);
    panelLayout->setSpacing(8);
    auto *brand = new QLabel("RiduxOS");
    brand->setObjectName("brand");
    panelLayout->addWidget(brand);
    panelLayout->addWidget(navButton("Apps", stack, 1, "panelButton"));
    panelLayout->addWidget(navButton("Files", stack, 2, "panelButton"));
    panelLayout->addWidget(navButton("Monitor", stack, 3, "panelButton"));
    panelLayout->addStretch(1);
    auto *clock = new QLabel;
    clock->setObjectName("clock");
    auto *timer = new QTimer(win);
    QObject::connect(timer, &QTimer::timeout, win, [clock]() {
        clock->setText(QDateTime::currentDateTime().toString("HH:mm"));
    });
    timer->start(1000);
    clock->setText(QDateTime::currentDateTime().toString("HH:mm"));
    panelLayout->addWidget(clock);
    panelLayout->addWidget(navButton("Power", stack, 4, "panelButton"));

    auto *dock = new QFrame;
    dock->setObjectName("dockRoot");
    auto *dockLayout = new QHBoxLayout(dock);
    dockLayout->setContentsMargins(10, 8, 10, 8);
    dockLayout->setSpacing(8);
    dockLayout->addStretch(1);
    dockLayout->addWidget(navButton("Dashboard", stack, 0, "dockButton"));
    dockLayout->addWidget(navButton("Apps", stack, 1, "dockButton"));
    dockLayout->addWidget(navButton("Files", stack, 2, "dockButton"));
    dockLayout->addWidget(navButton("Monitor", stack, 3, "dockButton"));
    dockLayout->addStretch(1);

    layout->addWidget(panel);
    layout->addWidget(stack, 1);
    layout->addWidget(dock);

    win->setCentralWidget(root);
    return win;
}

static QString shellMode(int argc, char **argv)
{
    QString mode;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith("--mode=")) {
            mode = arg.mid(7);
            break;
        }
        if (!arg.startsWith("--") && mode.isEmpty())
            mode = arg;
    }
    if (mode.isEmpty())
        mode = QFileInfo(QString::fromLocal8Bit(argv[0])).baseName();
    return mode.toLower();
}

int main(int argc, char **argv)
{
    const bool directKms = directKmsRequested(argc, argv);
    g_directKmsShell = directKms;
    std::fprintf(stderr, "[ridux-shell] start directKms=%d argc=%d\n", directKms ? 1 : 0, argc);
    ensureRiduxEnvironment(argc, argv);
#ifdef RIDUX_DIRECT_ONLY
    (void)directKms;
    QSurfaceFormat directFormat;
    directFormat.setRenderableType(QSurfaceFormat::OpenGLES);
    directFormat.setVersion(2, 0);
    directFormat.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(directFormat);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    const bool useQuick = envTruthy("RIDUX_QT_TRY_QUICK") && !envTruthy("RIDUX_QT_WIDGETS_FALLBACK");
    std::fprintf(stderr, "[ridux-shell] before direct-only QApplication widgets-fallback=%d quick=%d\n",
                 envTruthy("RIDUX_QT_WIDGETS_FALLBACK") ? 1 : 0, useQuick ? 1 : 0);
    QApplication app(argc, argv);
    app.setApplicationName("RiduxShellDirect");
    applyDirectPalette(app);
    if (envTruthy("RIDUX_QT_USE_STYLESHEET"))
        app.setStyleSheet(kRiduxStyle);
    app.setOverrideCursor(QCursor(Qt::ArrowCursor));
    std::fprintf(stderr, "[ridux-shell] after direct-only QApplication\n");
    if (envTruthy("RIDUX_QT_LEGACY_DIRECT_GL")) {
        DirectKmsWindow window;
        std::fprintf(stderr, "[ridux-shell] show direct-only legacy GL QWindow fullscreen\n");
        window.showFullScreen();
        std::fprintf(stderr, "[ridux-shell] entering direct-only legacy event loop\n");
        return app.exec();
    }
    if (!useQuick) {
        QMainWindow *window = makeDesktopSession();
        std::fprintf(stderr, "[ridux-shell] show direct-only QtWidgets fullscreen\n");
        window->showFullScreen();
        std::fprintf(stderr, "[ridux-shell] entering direct-only QtWidgets event loop\n");
        return app.exec();
    }

    QQuickView window;
    bool rendererLogged = false;
    int swappedFrames = 0;
    window.setTitle(QStringLiteral("RiduxOS Qt Quick KMS Shell"));
    window.setFlags(Qt::FramelessWindowHint);
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.setColor(QColor(244, 246, 248));
    window.setCursor(QCursor(Qt::ArrowCursor));
    QObject::connect(&window, &QQuickWindow::beforeRendering, &window, [&rendererLogged]() {
        if (rendererLogged)
            return;
        rendererLogged = true;
        if (!logCurrentHardwareRenderer("qtquick")) {
            QCoreApplication::exit(134);
        }
    }, Qt::DirectConnection);
    QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [&swappedFrames]() {
        if (swappedFrames < 12) {
            ++swappedFrames;
            std::fprintf(stderr, "[ridux-shell] qtquick frame swapped #%d\n", swappedFrames);
        }
    });
    QString qmlPath = QString::fromLocal8Bit(qgetenv("RIDUX_QT_SHELL_QML"));
    if (qmlPath.isEmpty())
        qmlPath = QStringLiteral("/usr/share/riduxui/ridux-shell.qml");
    const QFileInfo qmlInfo(qmlPath);
    if (!qmlInfo.exists()) {
        std::fprintf(stderr, "[ridux-shell] qml source missing path=%s\n",
                     qmlPath.toLocal8Bit().constData());
        return 134;
    }
    const QUrl qmlUrl = QUrl::fromLocalFile(qmlInfo.absoluteFilePath());
    configureQmlEngine(window, qmlInfo);
    window.setSource(qmlUrl);
    for (int i = 0; window.status() == QQuickView::Loading && i < 500; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (window.status() == QQuickView::Error) {
        logQmlErrors(window.errors());
        return 134;
    }
    QObject *rootObject = window.rootObject();
    if (!rootObject) {
        std::fprintf(stderr, "[ridux-shell] qml root object missing source=%s\n",
                     qmlUrl.toString().toLocal8Bit().constData());
        return 134;
    }
    if (QQuickItem *rootItem = qobject_cast<QQuickItem *>(rootObject)) {
        rootItem->setFocus(true);
        rootItem->forceActiveFocus();
    }
    std::fprintf(stderr, "[ridux-shell] show direct-only QtQuick fullscreen source=%s\n",
                 qmlPath.toLocal8Bit().constData());
    window.showFullScreen();
    std::fprintf(stderr, "[ridux-shell] entering direct-only event loop\n");
    return app.exec();
#else
    if (directKms) {
        std::fprintf(stderr, "[ridux-shell] before QGuiApplication\n");
        QGuiApplication app(argc, argv);
        app.setApplicationName("RiduxShell");
        std::fprintf(stderr, "[ridux-shell] after QGuiApplication\n");
        DirectKmsWindow window;
        std::fprintf(stderr, "[ridux-shell] show direct QWindow fullscreen\n");
        window.showFullScreen();
        std::fprintf(stderr, "[ridux-shell] entering direct event loop\n");
        return app.exec();
    }

    std::fprintf(stderr, "[ridux-shell] before QApplication\n");
    QApplication app(argc, argv);
    std::fprintf(stderr, "[ridux-shell] after QApplication\n");
    app.setApplicationName("RiduxShell");
    app.setStyleSheet(kRiduxStyle);

    const QString mode = shellMode(argc, argv);
    QList<QMainWindow *> windows;

    if (mode.contains("panel")) {
        windows << makePanel();
    } else if (mode.contains("dock")) {
        windows << makeDock();
    } else if (mode.contains("files")) {
        windows << makeFiles();
    } else if (mode.contains("monitor")) {
        windows << makeMonitor();
    } else if (mode.contains("dashboard") || mode.contains("control")) {
        windows << makeDashboard();
    } else {
        windows << makePanel();
        windows << makeDock();
        auto *dashboard = makeDashboard();
        const QRect screen = primaryGeometry();
        dashboard->move(screen.x() + qMax(20, (screen.width() - dashboard->width()) / 2),
                        screen.y() + qMax(80, (screen.height() - dashboard->height()) / 2));
        windows << dashboard;
    }

    for (QMainWindow *w : windows) {
        if (directKms) {
            std::fprintf(stderr, "[ridux-shell] show fullscreen\n");
            w->showFullScreen();
        } else {
            w->show();
        }
    }
    std::fprintf(stderr, "[ridux-shell] entering event loop\n");
    return app.exec();
#endif
}
