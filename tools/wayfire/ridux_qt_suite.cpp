#include <QtCore/QDateTime>
#include <QtCore/QFileInfo>
#include <QtCore/QRect>
#include <QtCore/QProcess>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QLinearGradient>
#include <QtGui/QPaintEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPen>
#include <QtGui/QScreen>
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
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include <cmath>

static const char *kAppStyle = R"CSS(
QMainWindow, QWidget {
    background: #eff1f4;
    color: #15191f;
    font-family: "DejaVu Sans", "Arial", sans-serif;
    font-size: 12px;
}
QLabel#title {
    font-size: 20px;
    font-weight: 700;
    color: #101419;
}
QLabel#subtitle {
    color: #5a626e;
}
QFrame#card {
    background: #ffffff;
    border: 1px solid #d6dae0;
    border-radius: 8px;
}
QPushButton {
    min-height: 30px;
    padding: 0 12px;
    border-radius: 6px;
    border: 1px solid #b8c0ca;
    background: #f7f8fa;
}
QPushButton:hover {
    background: #e8f1ff;
    border-color: #7aa7e8;
}
QProgressBar {
    border: 1px solid #c4cad2;
    border-radius: 5px;
    background: #eef1f5;
    text-align: center;
}
QProgressBar::chunk {
    border-radius: 4px;
    background: #2d7dd2;
}
QTableWidget, QListWidget {
    background: #ffffff;
    border: 1px solid #d6dae0;
    border-radius: 6px;
    selection-background-color: #dbeafe;
    selection-color: #101419;
}
QHeaderView::section {
    background: #e9edf2;
    border: 0;
    border-right: 1px solid #d6dae0;
    padding: 5px;
    font-weight: 600;
}
QWidget#panelRoot {
    background: #242830;
    border-bottom: 1px solid #59616c;
}
QWidget#dockRoot {
    background: #20252c;
    border: 1px solid #606977;
    border-radius: 8px;
}
QLabel#panelTitle {
    background: transparent;
    color: #f5f7fa;
    font-size: 13px;
    font-weight: 800;
}
QLabel#panelClock {
    background: transparent;
    color: #dce4ee;
    font-size: 12px;
    font-weight: 700;
}
QPushButton#panelButton, QPushButton#dockButton {
    color: #f5f7fa;
    min-height: 30px;
    padding: 0 12px;
    border-radius: 6px;
    border: 1px solid #5f6a78;
    background: #303842;
}
QPushButton#panelButton:hover, QPushButton#dockButton:hover {
    background: #355a8d;
    border-color: #8eb7ec;
}
QPushButton#dockButton {
    min-width: 74px;
    min-height: 40px;
}
)CSS";

class AnimatedPulseView : public QWidget {
public:
    explicit AnimatedPulseView(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(360, 190);
        setAutoFillBackground(false);
        connect(&timer_, &QTimer::timeout, this, [this]() {
            phase_ += 0.045;
            update();
        });
        timer_.start(16);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        (void)event;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect().adjusted(1, 1, -1, -1);
        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0.0, QColor(245, 247, 250));
        bg.setColorAt(1.0, QColor(210, 216, 224));
        p.fillRect(r, bg);

        QPen grid(QColor(170, 179, 190, 90));
        grid.setWidth(1);
        p.setPen(grid);
        for (int x = 20; x < width(); x += 40) p.drawLine(x, 0, x, height());
        for (int y = 20; y < height(); y += 40) p.drawLine(0, y, width(), y);

        const QPointF center(width() * 0.5, height() * 0.54);
        for (int i = 0; i < 6; ++i) {
            const qreal t = phase_ + i * 0.72;
            const qreal radius = 34 + 13 * std::sin(t * 1.3);
            const QPointF c(center.x() + std::cos(t) * 95,
                            center.y() + std::sin(t * 0.8) * 42);
            QColor color;
            if (i % 3 == 0) color = QColor(45, 125, 210, 185);
            else if (i % 3 == 1) color = QColor(43, 164, 124, 170);
            else color = QColor(226, 142, 55, 165);
            p.setPen(QPen(color.darker(125), 1));
            p.setBrush(color);
            p.drawEllipse(c, radius, radius);
        }

        QPainterPath path;
        path.moveTo(24, height() - 42);
        for (int x = 24; x < width() - 24; x += 12) {
            qreal y = height() - 52 - std::sin(phase_ * 2.2 + x * 0.035) * 20;
            path.lineTo(x, y);
        }
        p.setPen(QPen(QColor(32, 79, 122), 3));
        p.drawPath(path);

        p.setPen(QColor(22, 27, 33));
        p.setFont(QFont("DejaVu Sans", 10, QFont::DemiBold));
        p.drawText(QRectF(16, 12, width() - 32, 28),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   "Qt animated surface composed by Wayfire");
    }

private:
    QTimer timer_;
    qreal phase_ = 0.0;
};

static QFrame *card(const QString &title, const QString &value, const QString &detail) {
    auto *frame = new QFrame;
    frame->setObjectName("card");
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(5);

    auto *name = new QLabel(title);
    name->setStyleSheet("font-weight: 700; color: #313843;");
    auto *main = new QLabel(value);
    main->setStyleSheet("font-size: 22px; font-weight: 800; color: #111820;");
    auto *sub = new QLabel(detail);
    sub->setObjectName("subtitle");
    sub->setWordWrap(true);

    layout->addWidget(name);
    layout->addWidget(main);
    layout->addWidget(sub);
    return frame;
}

static QPushButton *commandButton(const QString &label, const QString &program) {
    auto *button = new QPushButton(label);
    QObject::connect(button, &QPushButton::clicked, button, [program]() {
        QProcess::startDetached(program, QStringList());
    });
    return button;
}

static QPushButton *shellButton(const QString &label, const QString &program, const char *objectName) {
    auto *button = commandButton(label, program);
    button->setObjectName(objectName);
    return button;
}

static QRect primaryScreenGeometry() {
    QScreen *screen = QApplication::primaryScreen();
    return screen ? screen->geometry() : QRect(0, 0, 1024, 768);
}

static QMainWindow *panelWindow() {
    const QRect screen = primaryScreenGeometry();
    auto *win = new QMainWindow;
    win->setWindowTitle("Ridux Qt Panel");
    win->setWindowFlags(Qt::FramelessWindowHint);

    auto *root = new QWidget;
    root->setObjectName("panelRoot");
    root->setAutoFillBackground(true);
    auto *layout = new QHBoxLayout(root);
    layout->setContentsMargins(12, 5, 12, 5);
    layout->setSpacing(8);

    auto *title = new QLabel("RiduxOS");
    title->setObjectName("panelTitle");
    layout->addWidget(title);
    layout->addSpacing(8);
    layout->addWidget(shellButton("Apps", "/usr/bin/ridux-open-launcher", "panelButton"));
    layout->addWidget(shellButton("Files", "/usr/bin/ridux-open-files", "panelButton"));
    layout->addWidget(shellButton("Terminal", "/usr/bin/ridux-terminal", "panelButton"));
    layout->addWidget(shellButton("Displays", "/usr/bin/ridux-display-settings", "panelButton"));
    layout->addStretch(1);

    auto *clock = new QLabel;
    clock->setObjectName("panelClock");
    auto *timer = new QTimer(win);
    QObject::connect(timer, &QTimer::timeout, win, [clock]() {
        clock->setText(QDateTime::currentDateTime().toString("HH:mm"));
    });
    timer->start(1000);
    clock->setText(QDateTime::currentDateTime().toString("HH:mm"));
    layout->addWidget(clock);
    layout->addWidget(shellButton("Power", "/usr/bin/ridux-power-menu", "panelButton"));

    win->setCentralWidget(root);
    const int width = qBound(640, screen.width() - 80, 920);
    win->resize(width, 54);
    win->move(screen.x() + (screen.width() - width) / 2, screen.y() + 20);
    return win;
}

static QMainWindow *dockWindow() {
    const QRect screen = primaryScreenGeometry();
    auto *win = new QMainWindow;
    win->setWindowTitle("Ridux Qt Dock");
    win->setWindowFlags(Qt::FramelessWindowHint);

    auto *root = new QWidget;
    root->setObjectName("dockRoot");
    root->setAutoFillBackground(true);
    auto *layout = new QHBoxLayout(root);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);
    layout->addWidget(shellButton("Apps", "/usr/bin/ridux-open-launcher", "dockButton"));
    layout->addWidget(shellButton("Files", "/usr/bin/ridux-open-files", "dockButton"));
    layout->addWidget(shellButton("Term", "/usr/bin/ridux-terminal", "dockButton"));
    layout->addWidget(shellButton("Shot", "/usr/bin/ridux-screenshot", "dockButton"));
    layout->addWidget(shellButton("Power", "/usr/bin/ridux-power-menu", "dockButton"));

    win->setCentralWidget(root);
    const int width = qBound(420, screen.width() - 96, 620);
    win->resize(width, 58);
    win->move(screen.x() + (screen.width() - width) / 2,
              screen.y() + qMax(0, screen.height() - 82));
    return win;
}

static QMainWindow *dashboardWindow() {
    auto *win = new QMainWindow;
    win->setWindowTitle("Ridux Qt Dashboard");
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel("Ridux Qt Dashboard");
    title->setObjectName("title");
    auto *subtitle = new QLabel("Wayfire session, Mesa GPU renderer, Qt Wayland client");
    subtitle->setObjectName("subtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);

    auto *grid = new QGridLayout;
    grid->setSpacing(10);
    grid->addWidget(card("Compositor", "Wayfire", "Server-side decoration and animated placement"), 0, 0);
    grid->addWidget(card("Renderer", "Mesa GL", "VirtualBox SVGA3D/VMSVGA or virtio GPU path"), 0, 1);
    grid->addWidget(card("Client", "Qt 6", "Wayland platform plugin loaded from rootfs"), 1, 0);
    grid->addWidget(card("Surface", "Qt Widgets", "Animated repaint surface composed by Wayfire"), 1, 1);
    layout->addLayout(grid);

    layout->addWidget(new AnimatedPulseView);

    auto *controls = new QGridLayout;
    controls->setSpacing(8);
    controls->addWidget(commandButton("Launcher", "/usr/bin/ridux-open-launcher"), 0, 0);
    controls->addWidget(commandButton("Files", "/usr/bin/ridux-open-files"), 0, 1);
    controls->addWidget(commandButton("Terminal", "/usr/bin/ridux-terminal"), 0, 2);
    controls->addWidget(commandButton("Displays", "/usr/bin/ridux-display-settings"), 1, 0);
    controls->addWidget(commandButton("Power", "/usr/bin/ridux-power-menu"), 1, 1);
    controls->addWidget(commandButton("Screenshot", "/usr/bin/ridux-screenshot"), 1, 2);
    layout->addLayout(controls);

    win->setCentralWidget(root);
    win->resize(660, 560);
    return win;
}

static QMainWindow *filesWindow() {
    auto *win = new QMainWindow;
    win->setWindowTitle("Ridux Qt Files");
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel("Ridux Qt Files");
    title->setObjectName("title");
    auto *subtitle = new QLabel("Small Qt file browser mock running as a normal Wayland toplevel");
    subtitle->setObjectName("subtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);

    auto *table = new QTableWidget(7, 4);
    table->setHorizontalHeaderLabels({"Name", "Type", "Size", "Modified"});
    const QStringList names = {
        "Desktop", "Documents", "Downloads", "Pictures",
        "ridux-session.log", "wayfire.ini", "gpu-ladder.txt"
    };
    const QStringList types = {
        "Folder", "Folder", "Folder", "Folder", "Log", "Config", "Report"
    };
    for (int i = 0; i < names.size(); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(names[i]));
        table->setItem(i, 1, new QTableWidgetItem(types[i]));
        table->setItem(i, 2, new QTableWidgetItem(i < 4 ? "-" : QString::number(24 + i * 7) + " KB"));
        table->setItem(i, 3, new QTableWidgetItem(QDateTime::currentDateTime().addSecs(-i * 310).toString("HH:mm:ss")));
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
    row->addStretch();
    layout->addLayout(row);

    win->setCentralWidget(root);
    win->resize(620, 440);
    return win;
}

static QMainWindow *monitorWindow() {
    auto *win = new QMainWindow;
    win->setWindowTitle("Ridux Qt Monitor");
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel("Ridux Qt Monitor");
    title->setObjectName("title");
    auto *subtitle = new QLabel("Live repainting widgets to make stutter easy to see");
    subtitle->setObjectName("subtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);

    QList<QProgressBar *> bars;
    const QStringList labels = {"Frame pacing", "GPU queue", "Wayland events", "Memory pressure"};
    for (const QString &label : labels) {
        auto *cardFrame = new QFrame;
        cardFrame->setObjectName("card");
        auto *row = new QHBoxLayout(cardFrame);
        row->setContentsMargins(12, 10, 12, 10);
        auto *name = new QLabel(label);
        name->setMinimumWidth(115);
        auto *bar = new QProgressBar;
        bar->setRange(0, 100);
        row->addWidget(name);
        row->addWidget(bar, 1);
        layout->addWidget(cardFrame);
        bars.push_back(bar);
    }

    auto *events = new QListWidget;
    events->addItems({
        "Wayland registry ready",
        "Qt platform: wayland",
        "Wayfire composing client surfaces",
        "Qt animated widget active"
    });
    layout->addWidget(events, 1);

    auto *timer = new QTimer(win);
    QObject::connect(timer, &QTimer::timeout, win, [bars, events]() {
        static int tick = 0;
        ++tick;
        for (int i = 0; i < bars.size(); ++i) {
            int value = 45 + int(std::sin((tick + i * 17) * 0.09) * 28.0) + i * 5;
            bars[i]->setValue(qBound(0, value, 100));
        }
        if (tick % 90 == 0) {
            events->insertItem(0, QDateTime::currentDateTime().toString("HH:mm:ss") + " repaint tick");
            while (events->count() > 8) delete events->takeItem(events->count() - 1);
        }
    });
    timer->start(33);

    win->setCentralWidget(root);
    win->resize(520, 430);
    return win;
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", qgetenv("QT_QPA_PLATFORM").isEmpty() ? QByteArray("wayland") : qgetenv("QT_QPA_PLATFORM"));
    QApplication app(argc, argv);
    app.setApplicationName("Ridux Qt Suite");
    app.setStyleSheet(kAppStyle);

    QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (mode.startsWith("--mode=")) mode = mode.mid(7);
    if (mode.isEmpty()) mode = QFileInfo(QString::fromLocal8Bit(argv[0])).baseName();

    QMainWindow *win = nullptr;
    if (mode.contains("panel", Qt::CaseInsensitive)) {
        win = panelWindow();
    } else if (mode.contains("dock", Qt::CaseInsensitive)) {
        win = dockWindow();
    } else if (mode.contains("files", Qt::CaseInsensitive)) {
        win = filesWindow();
    } else if (mode.contains("monitor", Qt::CaseInsensitive)) {
        win = monitorWindow();
    } else {
        win = dashboardWindow();
    }
    win->show();
    return app.exec();
}
