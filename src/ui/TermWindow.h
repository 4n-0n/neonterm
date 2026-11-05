#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

class HeaderBar;
class StatusLine;
class TermSurface;
class Session;
class QStackedWidget;
class LcarsView;
class QSettings;
class QShortcut;

class TermWindow : public QWidget
{
    Q_OBJECT
public:
    explicit TermWindow(QWidget *parent = nullptr);
    ~TermWindow() override;

    TermSurface *newTab(const QString &program = QString(), const QStringList &args = QStringList());
    TermSurface *newDemoTab(const QByteArray &bytes);
    void closeTab(int index);
    void activateTab(int index);
    void cycleTab(int delta);

    int tabCount() const { return int(m_tabs.size()); }
    int activeIndex() const { return m_active; }
    TermSurface *currentView() const;
    QStringList tabTitles() const;

    void applyFontSize(int px);
    int fontPixelSize() const { return m_fontPx; }
    void setCrt(bool on);
    bool crt() const { return m_crt; }

    HeaderBar *headerBar() const { return m_header; }

    // STUI: the LCARS face of the computer
    bool stui() const { return m_stui; }
    void setStui(bool on);
    LcarsView *lcars() const { return m_lcars; }
    StatusLine *statusLine() const { return m_status; }

    void toggleFullscreen();
    void setKeepOnExit(bool keep) { m_keepOnExit = keep; }
    void setStrokeFont(bool on);
    void setStartDirectory(const QString &dir) { m_startDir = dir; }
    void setUseOpenGL(bool on) { m_useGL = on; }
    bool useOpenGL() const { return m_useGL; }
    QString rendererInfo() const;
    QPixmap grabComposite();

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    struct Tab {
        Session *ses = nullptr;
        TermSurface *view = nullptr;
        QString title;
    };

    TermSurface *makeSurface();
    void updateChrome();
    void closeSession(Session *s, int code);
    void showContextMenu(const QPoint &globalPos);
    void saveSettings();
    QSettings *makeSettings();

    QVector<Tab> m_tabs;
    int m_active = -1;
    HeaderBar *m_header = nullptr;
    QStackedWidget *m_stack = nullptr;
    StatusLine *m_status = nullptr;
    int m_fontPx = 14;
    bool m_crt = true;
    QSettings *m_settings = nullptr;
    QShortcut *m_escape = nullptr;
    bool m_fullscreen = false;
    bool m_keepOnExit = false;
    bool m_useGL = true;
    bool m_stui = false;
    LcarsView *m_lcars = nullptr;
    bool m_strokeFont = false;
    QString m_startDir;
};
