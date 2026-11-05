#include "ui/TermWindow.h"

#include "core/Session.h"
#include "ui/HeaderBar.h"
#include "ui/LcarsView.h"
#include "ui/StatusLine.h"
#include "ui/GlTermView.h"
#include "ui/TermSurface.h"
#include "ui/TermView.h"
#include "ui/Theme.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

TermWindow::TermWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("neonterm"));
    setMinimumSize(780, 460);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_header = new HeaderBar(this);
    m_stack = new QStackedWidget(this);
    m_status = new StatusLine(this);
    lay->addWidget(m_header);
    lay->addWidget(m_stack, 1);
    lay->addWidget(m_status);

    connect(m_header, &HeaderBar::tabActivated, this, &TermWindow::activateTab);
    connect(m_header, &HeaderBar::tabCloseRequested, this, &TermWindow::closeTab);
    connect(m_header, &HeaderBar::newTabRequested, this, [this] { newTab(); });

    m_lcars = new LcarsView(this);
    m_stack->addWidget(m_lcars);
    connect(m_header, &HeaderBar::stuiToggled, this, &TermWindow::setStui);
    connect(m_lcars, &LcarsView::leaveRequested, this, [this] { setStui(false); });

    m_settings = makeSettings();
    // a new key: the 14 px saved by the previous version must not override the
    // new 1.2x default, and a deliberate zoom is still remembered
    m_fontPx = std::clamp(
        m_settings->value(QStringLiteral("ui/fontPxZoomed"), theme::scaled(14)).toInt(), 6, 72);
    m_crt = m_settings->value(QStringLiteral("fx/crt"), true).toBool();

    const QByteArray geom = m_settings->value(QStringLiteral("ui/geometry")).toByteArray();
    if (!geom.isEmpty())
        restoreGeometry(geom);
    else
        resize(1240, 780);
    m_status->setEffects(m_crt);
    m_status->setFontPixelSize(m_fontPx);

    auto bind = [this](const QKeySequence &key, std::function<void()> fn) {
        auto *sc = new QShortcut(key, this);
        connect(sc, &QShortcut::activated, this, fn);
        return sc;
    };

    bind(QKeySequence(QStringLiteral("Ctrl+Shift+T")), [this] { newTab(); });
    bind(QKeySequence(QStringLiteral("Ctrl+Shift+W")), [this] { closeTab(m_active); });
    bind(QKeySequence(QStringLiteral("Ctrl+Tab")), [this] { cycleTab(1); });
    bind(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), [this] { cycleTab(-1); });
    bind(QKeySequence(QStringLiteral("Ctrl+Shift+Right")), [this] { cycleTab(1); });
    bind(QKeySequence(QStringLiteral("Ctrl+Shift+Left")), [this] { cycleTab(-1); });
    bind(QKeySequence(QStringLiteral("F2")), [this] { setCrt(!m_crt); });
    bind(QKeySequence(QStringLiteral("F11")), [this] { toggleFullscreen(); });
    bind(QKeySequence(QStringLiteral("Ctrl++")), [this] { applyFontSize(m_fontPx + 1); });
    bind(QKeySequence(QStringLiteral("Ctrl+=")), [this] { applyFontSize(m_fontPx + 1); });
    bind(QKeySequence(QStringLiteral("Ctrl+-")), [this] { applyFontSize(m_fontPx - 1); });
    bind(QKeySequence(QStringLiteral("Ctrl+0")), [this] { applyFontSize(theme::scaled(14)); });
    for (int i = 1; i <= 9; ++i) {
        bind(QKeySequence(QStringLiteral("Alt+%1").arg(i)), [this, i] { activateTab(i - 1); });
    }

    m_escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    m_escape->setEnabled(false);
    connect(m_escape, &QShortcut::activated, this, [this] {
        if (m_fullscreen)
            toggleFullscreen();
    });
}

TermWindow::~TermWindow()
{
    saveSettings();
    delete m_settings;
}

QSettings *TermWindow::makeSettings()
{
    const QString dir = qEnvironmentVariable("NEONTERM_CONFIG_DIR");
    if (!dir.isEmpty())
        return new QSettings(dir + QStringLiteral("/neonterm.conf"), QSettings::IniFormat);
    return new QSettings(QStringLiteral("neonnet"), QStringLiteral("neonterm"));
}

void TermWindow::saveSettings()
{
    if (!m_settings)
        return;
    m_settings->setValue(QStringLiteral("ui/geometry"), saveGeometry());
    m_settings->setValue(QStringLiteral("ui/fontPxZoomed"), m_fontPx);
    m_settings->setValue(QStringLiteral("fx/crt"), m_crt);
    m_settings->sync();
}

QString TermWindow::rendererInfo() const
{
    if (m_active >= 0 && m_active < m_tabs.size()) {
        if (auto *gl = qobject_cast<GlTermView *>(m_tabs[m_active].view))
            return gl->rendererInfo();
    }
    return QStringLiteral("CPU blur");
}

QPixmap TermWindow::grabComposite()
{
    QPixmap shot = grab();
    // QWidget::grab() cannot read the QOpenGLWidget's framebuffer: composite it
    // only when the terminal page is the one on screen: with STUI up, the LCARS
    // face must not be covered by the hidden terminal's framebuffer
    auto *gl = qobject_cast<GlTermView *>(m_stack->currentWidget());
    if (gl && gl->glReady()) {
        QImage fb = gl->grabGlFrame();
        if (!fb.isNull()) {
            fb.setDevicePixelRatio(gl->devicePixelRatioF());
            QPainter p(&shot);
            p.drawImage(gl->mapTo(this, QPoint(0, 0)), fb);
        }
    }
    return shot;
}

void TermWindow::setStrokeFont(bool on)
{
    m_strokeFont = on;
    for (Tab &t : m_tabs) {
        if (auto *gl = qobject_cast<GlTermView *>(t.view))
            gl->setStrokeFont(on);
    }
}

TermSurface *TermWindow::makeSurface()
{
    if (m_useGL) {
        auto *gl = new GlTermView(m_stack);
        gl->setStrokeFont(m_strokeFont);
        return gl;
    }
    return new TermView(m_stack);
}

TermSurface *TermWindow::newTab(const QString &program, const QStringList &args)
{
    auto *ses = new Session(this);
    auto *view = makeSurface();
    view->setFontPixelSize(m_fontPx);
    view->setCrtEnabled(m_crt);
    view->setSession(ses);
    m_stack->addWidget(view);

    Tab t;
    t.ses = ses;
    t.view = view;
    m_tabs.push_back(t);
    const int index = int(m_tabs.size()) - 1;

    connect(ses, &Session::titleChanged, this, [this, ses](const QString &title) {
        for (Tab &tab : m_tabs) {
            if (tab.ses == ses)
                tab.title = title;
        }
        updateChrome();
    });
    connect(ses, &Session::exited, this, [this, ses](int code) { closeSession(ses, code); });
    connect(view, &TermSurface::fpsChanged, this, [this, view](double fps) {
        if (m_tabs.value(m_active).view == view)
            m_status->setFps(fps);
    });
    connect(view, &TermSurface::bell, this, [] { QApplication::beep(); });
    connect(view, &TermSurface::gridChanged, this, [this, view] {
        if (m_tabs.value(m_active).view == view)
            updateChrome();
    });
    connect(view, &TermSurface::contextMenuRequested, this, &TermWindow::showContextMenu);

    if (program.isEmpty())
        ses->startShell(80, 24, m_startDir);
    else
        ses->start(program, args, 80, 24, m_startDir);

    activateTab(index);
    return view;
}

TermSurface *TermWindow::newDemoTab(const QByteArray &bytes)
{
    auto *ses = new Session(this);
    auto *view = makeSurface();
    view->setFontPixelSize(m_fontPx);
    view->setCrtEnabled(m_crt);
    view->setSession(ses);
    m_stack->addWidget(view);

    Tab t;
    t.ses = ses;
    t.view = view;
    t.title = QStringLiteral("demo");
    m_tabs.push_back(t);
    const int index = int(m_tabs.size()) - 1;

    connect(view, &TermSurface::fpsChanged, this, [this, view](double fps) {
        if (m_tabs.value(m_active).view == view)
            m_status->setFps(fps);
    });
    connect(view, &TermSurface::contextMenuRequested, this, &TermWindow::showContextMenu);
    connect(view, &TermSurface::gridChanged, this, [this, view] {
        if (m_tabs.value(m_active).view == view)
            updateChrome();
    });

    view->show();
    ses->startDemo(bytes);
    activateTab(index);
    return view;
}

void TermWindow::closeTab(int index)
{
    if (index < 0 || index >= m_tabs.size())
        return;
    Tab t = m_tabs.takeAt(index);
    m_stack->removeWidget(t.view);
    t.view->setSession(nullptr);
    t.view->deleteLater();
    if (t.ses)
        t.ses->deleteLater();

    if (m_tabs.isEmpty()) {
        m_active = -1;
        close();
        return;
    }
    m_active = std::min(index, int(m_tabs.size()) - 1);
    activateTab(m_active);
}

void TermWindow::activateTab(int index)
{
    if (m_tabs.isEmpty())
        return;
    m_active = std::clamp(index, 0, int(m_tabs.size()) - 1);
    if (m_stui) {
        // the LCARS face stays on top while the tabs change underneath
        m_stack->setCurrentWidget(m_lcars);
        updateChrome();
        return;
    }
    m_stack->setCurrentWidget(m_tabs[m_active].view);
    m_tabs[m_active].view->setFocus(Qt::OtherFocusReason);
    updateChrome();
}

void TermWindow::setStui(bool on)
{
    if (m_stui == on)
        return;
    m_stui = on;
    m_header->setStui(on);
    if (on) {
        m_lcars->restart();
        m_stack->setCurrentWidget(m_lcars);
        m_lcars->setFocus(Qt::OtherFocusReason);
        m_settings->setValue(QStringLiteral("ui/stui"), true);
    } else {
        if (!m_tabs.isEmpty())
            m_stack->setCurrentWidget(m_tabs[m_active].view);
        if (auto *v = currentView())
            v->setFocus(Qt::OtherFocusReason);
        m_settings->setValue(QStringLiteral("ui/stui"), false);
    }
    updateChrome();
}

void TermWindow::cycleTab(int delta)
{
    if (m_tabs.size() < 2)
        return;
    const int n = int(m_tabs.size());
    activateTab(((m_active + delta) % n + n) % n);
}

TermSurface *TermWindow::currentView() const
{
    if (m_active < 0 || m_active >= m_tabs.size())
        return nullptr;
    return m_tabs[m_active].view;
}

QStringList TermWindow::tabTitles() const
{
    QStringList out;
    for (const Tab &t : m_tabs) {
        QString title = t.title;
        if (title.isEmpty()) {
            const QString prog = t.ses ? t.ses->program() : QString();
            title = prog.isEmpty() ? QStringLiteral("shell") : QFileInfo(prog).fileName();
        }
        out << title;
    }
    return out;
}

void TermWindow::updateChrome()
{
    const QStringList titles = tabTitles();
    m_header->setTabs(titles, m_active);

    if (m_active < 0 || m_active >= m_tabs.size()) {
        m_status->setSessionInfo(QStringLiteral("--"), 0, QSize());
        m_status->setState(QStringLiteral("IDLE"), false);
        setWindowTitle(QStringLiteral("neonterm"));
        return;
    }

    Tab &t = m_tabs[m_active];
    const QSize grid = t.view->gridSize();
    QString prog = t.ses->program();
    if (prog.isEmpty())
        prog = QStringLiteral("--");
    else
        prog = QFileInfo(prog).fileName();
    m_status->setSessionInfo(prog, int(t.ses->pid()), grid);
    m_status->setEffects(m_crt);
    m_status->setFontPixelSize(m_fontPx);
    const bool demo = t.ses->program() == QLatin1String("demo");
    m_status->setState(demo ? QStringLiteral("DEMO")
                            : (t.ses->running() ? QStringLiteral("RUNNING")
                                               : QStringLiteral("EXITED")),
                       demo || t.ses->running());
    m_header->setInfo(QStringLiteral("%1 · %2×%3")
                          .arg(m_useGL ? QStringLiteral("GL") : QStringLiteral("CPU"))
                          .arg(grid.width())
                          .arg(grid.height()));
    m_status->setRenderer(m_useGL ? QStringLiteral("OPENGL") : QStringLiteral("CPU BLUR"));
    setWindowTitle(QStringLiteral("%1 — neonterm").arg(titles.value(m_active)));
}

void TermWindow::closeSession(Session *s, int code)
{
    Q_UNUSED(code);
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].ses == s) {
            if (m_keepOnExit) {
                updateChrome();
                return;
            }
            closeTab(i);
            return;
        }
    }
}

void TermWindow::applyFontSize(int px)
{
    px = std::clamp(px, 6, 48);
    if (px == m_fontPx)
        return;
    m_fontPx = px;
    for (Tab &t : m_tabs)
        t.view->setFontPixelSize(px);
    updateChrome();
    saveSettings();
}

void TermWindow::setCrt(bool on)
{
    m_crt = on;
    for (Tab &t : m_tabs)
        t.view->setCrtEnabled(on);
    m_status->setEffects(m_crt);
    saveSettings();
}

void TermWindow::toggleFullscreen()
{
    m_fullscreen = !m_fullscreen;
    if (m_fullscreen) {
        m_header->hide();
        m_status->hide();
        showFullScreen();
    } else {
        m_header->show();
        m_status->show();
        showNormal();
    }
    m_escape->setEnabled(m_fullscreen);
}

void TermWindow::showContextMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu{background:#0a1118;color:#cfe9ff;border:1px solid #1d3c52;padding:4px;}"
        "QMenu::item{padding:5px 26px 5px 14px;border-radius:4px;}"
        "QMenu::item:selected{background:#123b52;}"
        "QMenu::separator{height:1px;background:#16283a;margin:4px 8px;}"));

    auto add = [this, &menu](const QString &text, std::function<void()> fn) {
        QAction *a = menu.addAction(text);
        connect(a, &QAction::triggered, this, fn);
    };

    TermSurface *v = currentView();
    add(QStringLiteral("Copia            Ctrl+Shift+C"), [v] {
        if (v)
            v->copySelection();
    });
    add(QStringLiteral("Incolla          Ctrl+Shift+V"), [v] {
        if (v)
            v->pasteClipboard();
    });
    menu.addSeparator();
    add(QStringLiteral("Nuova scheda     Ctrl+Shift+T"), [this] { newTab(); });
    add(QStringLiteral("Chiudi scheda    Ctrl+Shift+W"), [this] { closeTab(m_active); });
    menu.addSeparator();
    add(QStringLiteral("Zoom +           Ctrl++"), [this] { applyFontSize(m_fontPx + 1); });
    add(QStringLiteral("Zoom −           Ctrl+-"), [this] { applyFontSize(m_fontPx - 1); });
    add(QStringLiteral("STUI (LCARS)     Ctrl+Shift+S"), [this] { setStui(!m_stui); });
    add(QStringLiteral("Chiudi media     Ctrl+Shift+X"), [this] {
        if (auto *v = currentView())
            v->closeAllMedia();
    });
    add(QStringLiteral("Zoom 100%        Ctrl+0"), [this] { applyFontSize(theme::scaled(14)); });
    menu.addSeparator();
    add(m_crt ? QStringLiteral("Scanline: attive (F2)") : QStringLiteral("Scanline: spente (F2)"),
        [this] { setCrt(!m_crt); });
    add(QStringLiteral("Schermo intero   F11"), [this] { toggleFullscreen(); });

    menu.exec(globalPos);
}

void TermWindow::closeEvent(QCloseEvent *e)
{
    saveSettings();
    QWidget::closeEvent(e);
}
