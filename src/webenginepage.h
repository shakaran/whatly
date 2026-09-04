#ifndef WEBENGINEPAGE_H
#define WEBENGINEPAGE_H

#include <QAuthenticator>
#include <QDesktopServices>
#include <QFileDialog>
#include <QIcon>
#include <QImageReader>
#include <QMessageBox>
#include <QStyle>
#include <QWebEngineCertificateError>
#include <QWebEngineFullScreenRequest>
#include <QWebEngineNotification>
#include <QWebEnginePage>
#include <QWebEngineDesktopMediaRequest>
#include <QWebEngineProfile>
#include <QWebEngineRegisterProtocolHandlerRequest>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWebEnginePermission>

#include "settingsmanager.h"

#include "ui_certificateerrordialog.h"
#include "ui_passworddialog.h"

class WebEnginePage : public QWebEnginePage {
  Q_OBJECT
public:
  WebEnginePage(QWebEngineProfile *profile, QObject *parent = nullptr);

protected:
  bool acceptNavigationRequest(const QUrl &url,
                               QWebEnginePage::NavigationType type,
                               bool isMainFrame) override;
  QWebEnginePage *createWindow(QWebEnginePage::WebWindowType type) override;
  QStringList chooseFiles(FileSelectionMode mode, const QStringList &oldFiles,
                          const QStringList &acceptedMimeTypes) override;

  void handleCertificateError(const QWebEngineCertificateError &error);

  inline QWidget *view() {
      return QWebEngineView::forPage(this);
  }

  // Parent for the dialogs below. view() is null for a page that is not inside
  // a view — createWindow() used to hand back exactly such a page — and
  // dereferencing it is a crash. A null parent merely makes the dialog
  // top-level.
  QWidget *dialogParent();

public slots:
  void handlePermissionRequested(QWebEnginePermission permission);
  void handleDesktopMediaRequested(
      const QWebEngineDesktopMediaRequest &request);
  void handleLoadFinished(bool ok);

protected slots:
  void
  javaScriptConsoleMessage(WebEnginePage::JavaScriptConsoleMessageLevel level,
                           const QString &message, int lineId,
                           const QString &sourceId) override;
private slots:
  void handleAuthenticationRequired(const QUrl &requestUrl,
                                    QAuthenticator *auth);
  void handleProxyAuthenticationRequired(const QUrl &requestUrl,
                                         QAuthenticator *auth,
                                         const QString &proxyHost);
  void handleRegisterProtocolHandlerRequested(
      QWebEngineRegisterProtocolHandlerRequest request);
  void handleSelectClientCertificate(
      QWebEngineClientCertificateSelection clientCertSelection);
  void fullScreenRequestedByPage(QWebEngineFullScreenRequest request);
  void injectNewChatJavaScript();

private:
  // The WhatsApp-Web-loader-failure hint (issue #43) is emitted at most once per
  // page; the failure itself prints a long cascade of console errors.
  bool m_reportedWaLoadFailure = false;

  // The Service-Worker-cache recovery (clear caches + unregister + reload) is
  // attempted at most once per page, so a reload that fails the same way cannot
  // put us in a reload loop. See issue #43.
  bool m_attemptedSwRecovery = false;
  void recoverFromCorruptServiceWorker();
};

#endif // WEBENGINEPAGE_H
