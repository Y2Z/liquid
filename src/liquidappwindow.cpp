#include <QApplication>
#include <QBuffer>
#include <QDesktopWidget>
#include <QDir>
#include <QClipboard>
#include <QNetworkProxy>
#include <QtCore/qmath.h>
#include <QTimer>
#include <QWebEngineHistory>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>

#include "globals.h"

#include "liquidappcookiejar.hpp"
#include "liquidappwebpage.hpp"
#include "liquidappwindow.hpp"

LiquidAppWindow::LiquidAppWindow(QString* name) : QWebEngineView()
{
    // Prevent window from getting way too tiny
    setMinimumSize(LQD_APP_WIN_MIN_SIZE_W, LQD_APP_WIN_MIN_SIZE_H);

    // Disable default QWebEngineView's context menu
    setContextMenuPolicy(Qt::PreventContextMenu);

    liquidAppName = name;

    liquidAppConfig = new QSettings(QSettings::IniFormat,
                                    QSettings::UserScope,
                                    QString(PROG_NAME "%1" LQD_APPS_DIR_NAME).arg(QDir::separator()),
                                    *name,
                                    nullptr);

    // These default settings affect everything (including sub-frames)
    QWebEngineSettings *globalWebSettings = QWebEngineSettings::globalSettings();
    LiquidAppWebPage::setWebSettingsToDefault(globalWebSettings);

    liquidAppWebProfile = new QWebEngineProfile(QString(), this);
    liquidAppWebProfile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    liquidAppWebProfile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);

    if (!liquidAppWebProfile->isOffTheRecord()) {
        qDebug().noquote() << "Web profile is not off-the-record!";
    }

    liquidAppWebPage = new LiquidAppWebPage(liquidAppWebProfile, this);
    setPage(liquidAppWebPage);

    // Set default window title
    liquidAppWindowTitle = *liquidAppName;

    updateWindowTitle(*liquidAppName);

    // Pre-fill list of all possible zoom factors to snap to
    {
        for (qreal z = 1.0 - LQD_ZOOM_LVL_STEP; z >= LQD_ZOOM_LVL_MIN - LQD_ZOOM_LVL_STEP && z > 0; z -= LQD_ZOOM_LVL_STEP) {
            if (z >= LQD_ZOOM_LVL_MIN) {
                zoomFactors.prepend(z);
            } else {
                zoomFactors.prepend(LQD_ZOOM_LVL_MIN);
            }
        }

        if (LQD_ZOOM_LVL_MIN <= 1 && LQD_ZOOM_LVL_MAX >= 1) {
            zoomFactors.append(1.0);
        }

        for (qreal z = 1.0 + LQD_ZOOM_LVL_STEP; z <= LQD_ZOOM_LVL_MAX + LQD_ZOOM_LVL_STEP; z += LQD_ZOOM_LVL_STEP) {
            if (z <= LQD_ZOOM_LVL_MAX) {
                zoomFactors.append(z);
            } else {
                zoomFactors.append(LQD_ZOOM_LVL_MAX);
            }
        }
    }

    const QUrl startingUrl(liquidAppConfig->value(LQD_CFG_KEY_URL).toString());

    if (!startingUrl.isValid()) {
        qDebug().noquote() << "Invalid Liquid application URL:" << startingUrl;
        return;
    }

    liquidAppWebPage->addAllowedDomain(startingUrl.host());

    loadLiquidAppConfig();

    // Reveal Liquid app's window and bring it to front
    show();
    raise();
    activateWindow();

    // Connect keyboard shortcuts
    bindKeyboardShortcuts();

    // Initialize context menu
    setupContextMenu();

    // Allow page-level full screen happen
    connect(page(), &QWebEnginePage::fullScreenRequested, this, [](QWebEngineFullScreenRequest request) {
        request.accept();
    });

    // Trigger window title update if <title> changes
    connect(this, SIGNAL(titleChanged(QString)), SLOT(updateWindowTitle(QString)));

    // Update Liquid app's icon using the one provided by the website
    connect(page(), &QWebEnginePage::iconChanged, this, &LiquidAppWindow::onIconChanged);

    // Catch loading's start
    connect(page(), &QWebEnginePage::loadStarted, this, &LiquidAppWindow::loadStarted);

    // Catch loading's end
    connect(page(), &QWebEnginePage::loadFinished, this, &LiquidAppWindow::loadFinished);

    // Load Liquid app's starting URL
    load(startingUrl);
}

LiquidAppWindow::~LiquidAppWindow(void)
{
    saveLiquidAppConfig();

    delete liquidAppWebPage;
    delete liquidAppWebProfile;
}

void LiquidAppWindow::attemptToSetZoomFactorTo(const qreal desiredZoomFactor)
{
    int i = 0;
    const int ilen = zoomFactors.size();

    for (; i < ilen; i++) {
        if (qFuzzyCompare(zoomFactors[i], desiredZoomFactor)) {
            setZoomFactor(zoomFactors[i]);
            return;
        }
    }

    // Attempt to determine closest zoom level to snap to
    for (i = 0; i < ilen; i++) {
        if ((i == 0 || zoomFactors[i - 1] < desiredZoomFactor) && (i == ilen - 1 || zoomFactors[i + 1] > desiredZoomFactor)) {
            setZoomFactor(zoomFactors[i]);
            return;
        }
    }
}

void LiquidAppWindow::bindKeyboardShortcuts(void)
{
    // Connect window geometry lock shortcut
    toggleGeometryLockAction = new QAction;
    toggleGeometryLockAction->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_TOGGLE_WIN_GEOM_LOCK)));
    addAction(toggleGeometryLockAction);
    connect(toggleGeometryLockAction, SIGNAL(triggered()), this, SLOT(toggleWindowGeometryLock()));

    // Connect "mute audio" shortcut
    muteAudioAction = new QAction;
    muteAudioAction->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_MUTE_AUDIO)));
    addAction(muteAudioAction);
    connect(muteAudioAction, &QAction::triggered, this, [this](){
        page()->setAudioMuted(!page()->isAudioMuted());
        updateWindowTitle(title());
    });

    // Connect "go back" shortcut
    backAction = new QAction;
    backAction->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_GO_BACK)));
    addAction(backAction);
    connect(backAction, SIGNAL(triggered()), this, SLOT(back()));

    // Connect "go back" shortcut (backspace)
    backAction2 = new QAction;
    backAction2->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_GO_BACK_2)));
    addAction(backAction2);
    connect(backAction2, SIGNAL(triggered()), this, SLOT(back()));

    // Connect "go forward" shortcut
    forwardAction = new QAction;
    forwardAction->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_GO_FORWARD)));
    addAction(forwardAction);
    connect(forwardAction, SIGNAL(triggered()), this, SLOT(forward()));

    // Connect "reload" shortcut
    reloadAction = new QAction;
    reloadAction->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_RELOAD)));
    addAction(reloadAction);
    connect(reloadAction, SIGNAL(triggered()), this, SLOT(reload()));
    // Connect "alternative reload" shortcut (there can be only one QKeySequence per QAction)
    reloadAction2 = new QAction;
    reloadAction2->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_RELOAD_2)));
    addAction(reloadAction2);
    connect(reloadAction2, SIGNAL(triggered()), this, SLOT(reload()));

    // Connect "hard reload" shortcut
    hardReloadAction = new QAction;
    hardReloadAction->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_HARD_RELOAD)));
    addAction(hardReloadAction);
    connect(hardReloadAction, SIGNAL(triggered()), this, SLOT(hardReload()));

    // Connect "toggle full screen" shortcut
    toggleFullScreenModeAction = new QAction;
    toggleFullScreenModeAction->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_TOGGLE_FS_MODE)));
    addAction(toggleFullScreenModeAction);
    connect(toggleFullScreenModeAction, SIGNAL(triggered()), this, SLOT(toggleFullScreenMode()));
    // Connect "alternative toggle full screen" shortcut (there can be only one QKeySequence per QAction)
    toggleFullScreenModeAction2 = new QAction;
    toggleFullScreenModeAction2->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_TOGGLE_FS_MODE_2)));
    addAction(toggleFullScreenModeAction2);
    connect(toggleFullScreenModeAction2, SIGNAL(triggered()), this, SLOT(toggleFullScreenMode()));

    // Connect "stop loading" / "exit full screen mode" shortcut
    stopLoadingOrExitFullScreenModeAction = new QAction;
    stopLoadingOrExitFullScreenModeAction->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_STOP_OR_EXIT_FS_MODE)));
    addAction(stopLoadingOrExitFullScreenModeAction);
    connect(stopLoadingOrExitFullScreenModeAction, SIGNAL(triggered()), this, SLOT(stopLoadingOrExitFullScreenMode()));

    // Connect "zoom in" shortcut
    zoomInAction = new QAction;
    zoomInAction->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_ZOOM_LVL_INC)));
    addAction(zoomInAction);
    connect(zoomInAction, SIGNAL(triggered()), this, SLOT(zoomIn()));

    // Connect "zoom out" shortcut
    zoomOutAction = new QAction;
    zoomOutAction->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_ZOOM_LVL_DEC)));
    addAction(zoomOutAction);
    connect(zoomOutAction, SIGNAL(triggered()), this, SLOT(zoomOut()));

    // Connect "reset zoom" shortcut
    zoomResetAction = new QAction;
    zoomResetAction->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_ZOOM_LVL_RESET)));
    addAction(zoomResetAction);
    connect(zoomResetAction, SIGNAL(triggered()), this, SLOT(zoomReset()));

    // Connect "exit" shortcut
    quitAction = new QAction;
    quitAction->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_QUIT)));
    addAction(quitAction);
    connect(quitAction, SIGNAL(triggered()), this, SLOT(close()));

    // Connect "alternative exit" shortcut
    quitAction2 = new QAction;
    quitAction2->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_QUIT_2)));
    addAction(quitAction2);
    connect(quitAction2, SIGNAL(triggered()), this, SLOT(close()));

    // Connect "take snapshot" shortcut
    takeSnapshotAction = new QAction;
    takeSnapshotAction->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_TAKE_SNAPSHOT)));
    addAction(takeSnapshotAction);
    connect(takeSnapshotAction, SIGNAL(triggered()), this, SLOT(takeSnapshotSlot()));

    // Connect "take full page snapshot" shortcut
    takeSnapshotFullPageAction = new QAction;
    takeSnapshotFullPageAction->setShortcut(QKeySequence(tr(LQD_KBD_SEQ_TAKE_SNAPSHOT_FULL)));
    addAction(takeSnapshotFullPageAction);
    connect(takeSnapshotFullPageAction, SIGNAL(triggered()), this, SLOT(takeSnapshotFullPageSlot()));

    // Make it possible to intercept zoom events
    QApplication::instance()->installEventFilter(this);
}

void LiquidAppWindow::closeEvent(QCloseEvent* event)
{
    event->accept();
    deleteLater();
}

void LiquidAppWindow::contextMenuEvent(QContextMenuEvent* event)
{
    (void)event;

    contextMenuBackAction->setEnabled(history()->canGoBack());
    contextMenuForwardAction->setEnabled(history()->canGoForward());

    contextMenu->exec(QCursor::pos());
}

bool LiquidAppWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched->parent() == this) {
        switch (event->type()) {
            case QEvent::Wheel:
                if (handleWheelEvent(static_cast<QWheelEvent*>(event))) {
                    return true;
                }
                break;

            default:
                break;
        }
    }

    return QWebEngineView::eventFilter(watched, event);
}

void LiquidAppWindow::exitFullScreenMode(void)
{
    // Exit from full screen mode
    setWindowState(windowState() & ~Qt::WindowFullScreen);

    if (windowGeometryIsLocked) {
        // Pause here to wait for any kind of window resize animations to finish
        mSleep(200);

        setMinimumSize(width(), height());
        setMaximumSize(width(), height());
    }
}

bool LiquidAppWindow::handleWheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        (event->delta() > 0) ? zoomIn() : zoomOut();
        event->accept();
        return true;
    }

    return false;
}

void LiquidAppWindow::hardReload(void)
{
    // TODO: if JS enabled, stop all currently running JS (destroy web workers, promises, etc)

    // Synchronously wipe all document contents (page's setContent() and setHtml() are aynchrnonous, can't use them here)
    const QString js = QString("(()=>{"\
                                   "let e=document.firstElementChild;"\
                                   "if(e){"\
                                       "e.remove()"\
                                   "}"\
                               "})()");
    page()->runJavaScript(js, QWebEngineScript::ApplicationWorld);

    // Ensure that while this Liquid App is being reset, the window title remains to be set to this Liquid App's name
    // to mimic the same experience that happens when the user initially runs this Liquid app
    if (!liquidAppWindowTitleIsReadOnly) {
        liquidAppWindowTitle = *liquidAppName;

        const QString js = QString("(()=>{"\
                                       "let e=document.createElement('title');"\
                                       "e.innerText='%1';"\
                                       "document.appendChild(e)"\
                                   "})()").arg(liquidAppWindowTitle.replace("'", "\\'"));
        page()->runJavaScript(js, QWebEngineScript::ApplicationWorld);
    }

    updateWindowTitle(title());

    // TODO: reset localStorage / Cookies in case they're disabled?

    // TODO: clear any type of cache, if possible

    QUrl url(liquidAppConfig->value(LQD_CFG_KEY_URL).toString(), QUrl::StrictMode);
    setUrl(url);
}

void LiquidAppWindow::loadFinished(bool ok)
{
    pageIsLoading = false;

    if (ok) {
        pageHasError = false;
    } else {
        if (forgiveNextPageLoadError) {
            pageHasError = false;
        } else {
            pageHasError = true;
        }
    }

    // Unset forgiveNextPageLoadError
    if (forgiveNextPageLoadError) {
        forgiveNextPageLoadError = false;
    }

    updateWindowTitle(title());
}

void LiquidAppWindow::loadLiquidAppConfig(void)
{
    if (liquidAppConfig->contains(LQD_CFG_KEY_TITLE)) {
        liquidAppWindowTitle = liquidAppConfig->value(LQD_CFG_KEY_TITLE).toString();
        // Make sure the window title never gets changed
        liquidAppWindowTitleIsReadOnly = true;
    }

    // Apply network proxy configuration
    if (liquidAppConfig->contains(LQD_CFG_KEY_USE_PROXY)) {
        proxy = new QNetworkProxy;

        if (liquidAppConfig->value(LQD_CFG_KEY_USE_PROXY, false).toBool()) {
            const bool isSocks = liquidAppConfig->value(LQD_CFG_KEY_PROXY_USE_SOCKS).toBool();

            proxy->setType((isSocks) ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy);

            if (liquidAppConfig->contains(LQD_CFG_KEY_PROXY_HOST)) {
                proxy->setHostName(liquidAppConfig->value(LQD_CFG_KEY_PROXY_HOST).toString());
            }

            if (liquidAppConfig->contains(LQD_CFG_KEY_PROXY_PORT)) {
                proxy->setPort(liquidAppConfig->value(LQD_CFG_KEY_PROXY_PORT).toInt());
            }

            if (liquidAppConfig->value(LQD_CFG_KEY_PROXY_USE_AUTH, false).toBool()) {
                if (liquidAppConfig->contains(LQD_CFG_KEY_PROXY_USER_NAME)) {
                    proxy->setUser(liquidAppConfig->value(LQD_CFG_KEY_PROXY_USER_NAME).toString());
                }

                if (liquidAppConfig->contains(LQD_CFG_KEY_PROXY_USER_PASSWORD)) {
                    proxy->setPassword(liquidAppConfig->value(LQD_CFG_KEY_PROXY_USER_PASSWORD).toString());
                }
            }
        } else {
            proxy->setType(QNetworkProxy::NoProxy);
        }

        QNetworkProxy::setApplicationProxy(*proxy);
    }

    // Remove window manager's frame
    {
        if (liquidAppConfig->value(LQD_CFG_KEY_REMOVE_WINDOW_FRAME, false).toBool()) {
            setWindowFlags(Qt::FramelessWindowHint);
        }
    }

    // Set the page's background color behind the document's body
    {
        if (liquidAppConfig->value(LQD_CFG_KEY_USE_CUSTOM_BG, false).toBool() && liquidAppConfig->contains(LQD_CFG_KEY_CUSTOM_BG_COLOR)) {
            const QColor backgroundColor = QColor(QRgba64::fromRgba64(liquidAppConfig->value(LQD_CFG_KEY_CUSTOM_BG_COLOR).toString().toULongLong(Q_NULLPTR, 16)));

            if (backgroundColor.alpha() < 255) {
                // Make window background transparent
                setAttribute(Qt::WA_TranslucentBackground);
            }

            page()->setBackgroundColor(backgroundColor);
        } else {
            page()->setBackgroundColor(LQD_DEFAULT_BG_COLOR);
        }
    }

    // Determine where this Liquid app is allowed to navigate, and what should be opened in external browser
    if (liquidAppConfig->contains(LQD_CFG_KEY_ADDITIONAL_DOMAINS)) {
        liquidAppWebPage->addAllowedDomains(
            liquidAppConfig->value(LQD_CFG_KEY_ADDITIONAL_DOMAINS).toString().split(" ")
        );
    }

    // Deal with Cookies
    {
        LiquidAppCookieJar *liquidAppCookieJar = new LiquidAppCookieJar(this);
        QWebEngineCookieStore *cookieStore = page()->profile()->cookieStore();

        connect(cookieStore, &QWebEngineCookieStore::cookieAdded, liquidAppCookieJar, &LiquidAppCookieJar::upsertCookie);
        connect(cookieStore, &QWebEngineCookieStore::cookieRemoved, liquidAppCookieJar, &LiquidAppCookieJar::removeCookie);

        liquidAppCookieJar->restoreCookies(cookieStore);
    }

    // Restore window geometry
    if (liquidAppConfig->contains(LQD_CFG_KEY_WIN_GEOM)) {
        restoreGeometry(QByteArray::fromHex(
            liquidAppConfig->value(LQD_CFG_KEY_WIN_GEOM).toByteArray()
        ));
    } else {
        const QDesktopWidget widget;
        const QRect currentScreenSize = widget.availableGeometry(widget.primaryScreen());
        const int currentScreenWidth = currentScreenSize.width();
        const int currentScreenHeight = currentScreenSize.height();
        setGeometry(currentScreenWidth / 4, currentScreenHeight / 4, currentScreenWidth / 2, currentScreenHeight / 2);
    }

    // Toggle JavaScript on if enabled in application config
    if (liquidAppConfig->contains(LQD_CFG_KEY_ENABLE_JS)) {
        settings()->setAttribute(
            QWebEngineSettings::JavascriptEnabled,
            liquidAppConfig->value(LQD_CFG_KEY_ENABLE_JS).toBool()
        );
    }

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    // Hide scroll bars
    if (liquidAppConfig->contains(LQD_CFG_KEY_HIDE_SCROLL_BARS)) {
        settings()->setAttribute(
            QWebEngineSettings::ShowScrollBars,
            !liquidAppConfig->value(LQD_CFG_KEY_HIDE_SCROLL_BARS).toBool()
        );
    }
#endif

    // Mute audio if muted in application config
    if (liquidAppConfig->contains(LQD_CFG_KEY_MUTE_AUDIO)) {
        page()->setAudioMuted(liquidAppConfig->value(LQD_CFG_KEY_MUTE_AUDIO).toBool());
    }

    // Restore web view zoom level
    if (liquidAppConfig->contains(LQD_CFG_KEY_ZOOM_LVL)) {
        attemptToSetZoomFactorTo(liquidAppConfig->value(LQD_CFG_KEY_ZOOM_LVL).toDouble());

        // There's a bug in Qt, using QTimer seems to be the only solution
        QTimer::singleShot(1000, [&](){
            if (liquidAppConfig->contains(LQD_CFG_KEY_ZOOM_LVL)) {
                attemptToSetZoomFactorTo(liquidAppConfig->value(LQD_CFG_KEY_ZOOM_LVL).toDouble());
            }
        });
    }

    // Lock for the app's window's geometry
    if (liquidAppConfig->contains(LQD_CFG_KEY_LOCK_WIN_GEOM)) {
        if (liquidAppConfig->value(LQD_CFG_KEY_LOCK_WIN_GEOM).toBool()) {
            toggleWindowGeometryLock();
            windowGeometryIsLocked = true;
        }
    }

    // Custom user-agent string
    if (liquidAppConfig->contains(LQD_CFG_KEY_USER_AGENT)) {
        liquidAppWebProfile->setHttpUserAgent(liquidAppConfig->value(LQD_CFG_KEY_USER_AGENT).toString());
    }

    // Additional user-defined CSS (does't require JavaScript enabled in order to work)
    if (liquidAppConfig->contains(LQD_CFG_KEY_ADDITIONAL_CSS)) {
        QString additionalCss = liquidAppConfig->value(LQD_CFG_KEY_ADDITIONAL_CSS).toString();
        const QString js = QString("(()=>{"\
                                       "const styleEl = document.createElement('style');"\
                                       "const cssTextNode = document.createTextNode('%1');"\
                                       "styleEl.appendChild(cssTextNode);"\
                                       "document.head.appendChild(styleEl)"\
                                   "})()").arg(additionalCss.replace("\n", " ").replace("'", "\\'"));
        QWebEngineScript script;
        script.setInjectionPoint(QWebEngineScript::DocumentReady);
        script.setRunsOnSubFrames(false);
        script.setSourceCode(js);
        script.setWorldId(QWebEngineScript::ApplicationWorld);
        liquidAppWebPage->scripts().insert(script);
    }

    // Additional user-defined JS (does't require JavaScript enabled in order to work)
    if (liquidAppConfig->contains(LQD_CFG_KEY_ADDITIONAL_JS)) {
        QString js = liquidAppConfig->value(LQD_CFG_KEY_ADDITIONAL_JS).toString();
        QWebEngineScript script;
        script.setInjectionPoint(QWebEngineScript::DocumentReady);
        script.setRunsOnSubFrames(false);
        script.setSourceCode(js);
        script.setWorldId(QWebEngineScript::ApplicationWorld);
        liquidAppWebPage->scripts().insert(script);
    }

#if !defined(Q_OS_LINUX) && !defined(Q_OS_UNIX) // This doesn't work on X11
    // Set window icon
    if (liquidAppConfig->contains(LQD_CFG_KEY_ICON)) {
        QIcon liquidAppIcon;
        QByteArray byteArray = QByteArray::fromHex(
            liquidAppConfig->value(LQD_CFG_KEY_ICON).toByteArray()
        );
        QBuffer buffer(&byteArray);
        buffer.open(QIODevice::ReadOnly);
        QDataStream in(&buffer);
        in >> liquidAppIcon;
        buffer.close();
        window()->setWindowIcon(liquidAppIcon);
    }
#endif
}

void LiquidAppWindow::loadStarted(void)
{
    pageIsLoading = true;
    pageHasError = false;

    updateWindowTitle(title());
}

void LiquidAppWindow::mSleep(const int ms)
{
    QTime proceedAfter = QTime::currentTime().addMSecs(ms);

    while (QTime::currentTime() < proceedAfter) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, ms / 4);
    }
}

void LiquidAppWindow::moveEvent(QMoveEvent *event)
{
    // Remember window position
    liquidAppWindowGeometry = saveGeometry();

    QWebEngineView::moveEvent(event);
}

void LiquidAppWindow::onIconChanged(QIcon icon)
{
    // Set window icon
    setWindowIcon(icon);

    // Save icon in settings
    if (!liquidAppConfig->contains(LQD_CFG_KEY_ICON)) {
        QByteArray byteArray;
        QBuffer buffer(&byteArray);
        buffer.open(QIODevice::WriteOnly);
        QDataStream out(&buffer);
        out << icon;
        buffer.close();
        // TODO: move into saveLiquidAppConfig()
        liquidAppConfig->setValue(LQD_CFG_KEY_ICON, QString(byteArray.toHex()));
        liquidAppConfig->sync();
    }
}

void LiquidAppWindow::resizeEvent(QResizeEvent* event)
{
    // Remember window size (unless in full screen mode)
    if (!isFullScreen()) {
        // Pause here to wait for any kind of window resize animations to finish
        mSleep(200);

        liquidAppWindowGeometry = saveGeometry();
    }

    QWebEngineView::resizeEvent(event);
}

void LiquidAppWindow::saveLiquidAppConfig(void)
{
    if (qFuzzyCompare(zoomFactor(), 1.0)) {
        if (liquidAppConfig->contains(LQD_CFG_KEY_ZOOM_LVL)) {
            liquidAppConfig->remove(LQD_CFG_KEY_ZOOM_LVL);
        }
    } else {
        liquidAppConfig->setValue(LQD_CFG_KEY_ZOOM_LVL, zoomFactor());
    }

    if (page()->isAudioMuted()) {
        liquidAppConfig->setValue(LQD_CFG_KEY_MUTE_AUDIO, true);
    } else {
        if (liquidAppConfig->contains(LQD_CFG_KEY_MUTE_AUDIO)) {
            liquidAppConfig->remove(LQD_CFG_KEY_MUTE_AUDIO);
        }
    }

    if (!isFullScreen()) {
        liquidAppConfig->setValue(LQD_CFG_KEY_WIN_GEOM, QString(liquidAppWindowGeometry.toHex()));
    }

    if (windowGeometryIsLocked) {
        liquidAppConfig->setValue(LQD_CFG_KEY_LOCK_WIN_GEOM, true);
    } else {
        if (liquidAppConfig->contains(LQD_CFG_KEY_LOCK_WIN_GEOM)) {
            liquidAppConfig->remove(LQD_CFG_KEY_LOCK_WIN_GEOM);
        }
    }

    liquidAppConfig->sync();
}

void LiquidAppWindow::setupContextMenu(void)
{
    contextMenu = new QMenu;

    contextMenuCopyUrlAction = new QAction(QIcon::fromTheme(QStringLiteral("internet-web-browser")), tr("Copy Current URL"));
    contextMenuReloadAction = new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Refresh"));
    contextMenuBackAction = new QAction(QIcon::fromTheme(QStringLiteral("go-previous")), tr("Go Back"));
    contextMenuForwardAction = new QAction(QIcon::fromTheme(QStringLiteral("go-next")), tr("Go Forward"));
    contextMenuCloseAction = new QAction(QIcon::fromTheme(QStringLiteral("process-stop")), tr("Quit"));

    contextMenu->addAction(contextMenuCopyUrlAction);
    contextMenu->addAction(contextMenuReloadAction);
    contextMenu->addAction(contextMenuBackAction);
    contextMenu->addAction(contextMenuForwardAction);
    contextMenu->addAction(contextMenuCloseAction);

    connect(contextMenuCopyUrlAction, &QAction::triggered, this, [this](){
        QApplication::clipboard()->setText(page()->url().toString());
    });
    connect(contextMenuReloadAction, &QAction::triggered, this, &QWebEngineView::reload);
    connect(contextMenuBackAction, &QAction::triggered, this, &QWebEngineView::back);
    connect(contextMenuForwardAction, &QAction::triggered, this, &QWebEngineView::forward);
    connect(contextMenuCloseAction, SIGNAL(triggered()), this, SLOT(close()));

    setContextMenuPolicy(Qt::DefaultContextMenu);
}

void LiquidAppWindow::setForgiveNextPageLoadError(const bool ok)
{
    forgiveNextPageLoadError = ok;
}

void LiquidAppWindow::stopLoadingOrExitFullScreenMode(void)
{
    if (pageIsLoading) {
        triggerPageAction(QWebEnginePage::Stop);
    } else {
        exitFullScreenMode();
    }
}

void LiquidAppWindow::takeSnapshotSlot(void)
{
    takeSnapshot(false);
}
void LiquidAppWindow::takeSnapshotFullPageSlot(void)
{
    takeSnapshot(true);
}

void LiquidAppWindow::takeSnapshot(const bool fullPage)
{
    QImage* image;

    // TODO: add camera flash visual effect
    // TODO: add shutter sound

    if (fullPage) {
        image = new QImage((page()->contentsSize() / QPaintDevice::devicePixelRatio()).toSize(), QImage::Format_ARGB32);
    } else {
        image = new QImage(contentsRect().size(), QImage::Format_ARGB32);
    }

    image->fill(Qt::transparent);

    QPainter* painter = new QPainter(image);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->setRenderHint(QPainter::HighQualityAntialiasing);
    painter->setRenderHint(QPainter::NonCosmeticDefaultPen);

    if (fullPage) {
        // NOTE: we cannot just resize the view to contents to get a full page snapshot done (responsive websites may produce different result);
        //       instead, the best way seems to be to scroll through the page region-by-region, and compose the final image out of those chunks
        //       (this seems to be the best way to go, until QWebEngineView provides a way to render parts that aren't in the viewport)

        // Remember initial scroll position to be able to come back to it after the whole page is captured
        const QPointF initScrollPos = page()->scrollPosition() / QPaintDevice::devicePixelRatio();

        // Calculate how many zones we're going to have to scroll to
        QPoint* zoneResolution = new QPoint(qCeil(qreal(image->width()) / width()), qCeil(qreal(image->height()) / height()));

        // Calculate non-overlapping rectangles that we're gonna scroll to and capture one-by-one
        QList<QRect*> zones;
        {
            for (int iy = 0; iy < zoneResolution->y(); iy++) {
                for (int ix = 0; ix < zoneResolution->x(); ix++) {
                    zones.append(new QRect(
                        ix * width(),
                        iy * height(),
                        (ix + 1 != zoneResolution->x()) ? width() : (image->width() - ix * width()),
                        (iy + 1 != zoneResolution->y()) ? height() : (image->height() - iy * height())
                    ));
                }
            }
        }

        static const QString js = "window.scrollTo(%1, %2);";

        for (int iz = 0; iz < zoneResolution->x() * zoneResolution->y(); iz++) {
            QRect* zone = zones[iz];
            page()->runJavaScript(QString(js).arg(zone->x()).arg(zone->y()), QWebEngineScript::ApplicationWorld);

            // Give a bit of time to QWebEngineView to catch up with the scroll offset change that was made by runJavaScript()
            mSleep(25);

            QRegion region(width() - zone->width(), height() - zone->height(), zone->width(), zone->height());
            render(painter, QPoint(zone->x(), zone->y()), region);

            delete zone;
        }

        // Scroll the web view back to where it was before we started taking full page snapshot
        page()->runJavaScript(QString(js).arg(initScrollPos.x()).arg(initScrollPos.y()), QWebEngineScript::ApplicationWorld);
    } else {
        render(painter);
    }

    // TODO: add option to take snapshots in SVG format (kinda like html2canvas, but vector)

    painter->end();
    delete painter;

    {
        const QString path = QDir::homePath() + QDir::separator() + "Pictures";
        QDir dir(path);
        if (!dir.exists()) {
            dir.mkdir(".");
        }

        const QString fileName = QString("%1 of Liquid App %2 taken on %3 at %4.png")
                                    .arg(tr(fullPage ? "Full page snapshot" : "Snapshot"))
                                    .arg(*liquidAppName)
                                    .arg(QDateTime::currentDateTimeUtc().toString(QLocale().dateFormat()))
                                    .arg(QDateTime::currentDateTimeUtc().toString(QLocale().timeFormat()));
        image->save(path + QDir::separator() + fileName, "PNG");
    }

    // TODO: add EXIF?

    delete image;
}

void LiquidAppWindow::toggleFullScreenMode(void)
{
    if (isFullScreen()) {
        exitFullScreenMode();
    } else {
        // Make it temporarily possible to resize the window if geometry is locked
        if (windowGeometryIsLocked) {
            setMinimumSize(LQD_APP_WIN_MIN_SIZE_W, LQD_APP_WIN_MIN_SIZE_H);
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        }
        // Enter the full screen mode
        setWindowState(windowState() | Qt::WindowFullScreen);
    }
}

void LiquidAppWindow::toggleWindowGeometryLock(void)
{
    // Prevent toggling window geometry lock while in full screen mode
    if (!isFullScreen()) {
        if (windowGeometryIsLocked) {
            // Open up resizing restrictions
            setMinimumSize(LQD_APP_WIN_MIN_SIZE_W, LQD_APP_WIN_MIN_SIZE_H);
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            windowGeometryIsLocked = false;
        } else {
            // Lock down resizing
            setMinimumSize(width(), height());
            setMaximumSize(width(), height());
            windowGeometryIsLocked = true;
        }

        liquidAppConfig->sync();
    }

    updateWindowTitle(title());
}

void LiquidAppWindow::updateWindowTitle(const QString title)
{
    QString textIcons;

    if (!liquidAppWindowTitleIsReadOnly) {
        if (title.size() > 0) {
            liquidAppWindowTitle = title;
        } else {
            liquidAppWindowTitle = *liquidAppName;
        }
    }

    if (windowGeometryIsLocked) {
        textIcons.append(LQD_ICON_LOCKED);
    }
    if (page()->isAudioMuted()) {
        textIcons.append(LQD_ICON_MUTED);
    }
    if (pageIsLoading) {
        textIcons.append(LQD_ICON_LOADING);
    } else {
        if (pageHasError) {
            textIcons.append(LQD_ICON_ERROR);
        }
    }

    if (textIcons != "") {
        textIcons = " " + textIcons;
    }

    setWindowTitle(liquidAppWindowTitle + textIcons);
}

void LiquidAppWindow::zoomIn(void)
{
    attemptToSetZoomFactorTo(zoomFactor() + LQD_ZOOM_LVL_STEP);
}

void LiquidAppWindow::zoomOut(void)
{
    attemptToSetZoomFactorTo(zoomFactor() - LQD_ZOOM_LVL_STEP);
}

void LiquidAppWindow::zoomReset(void)
{
    attemptToSetZoomFactorTo(1.0);
}
