#include "about.h"
#include <QClipboard>
#include <QMessageBox>
#include <QUrlQuery>
#include <QGuiApplication>
#include "ui_about.h"
#include <QDesktopServices>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QUrl>
#include <utils.h>

About::About(QWidget *parent) : QWidget(parent), ui(new Ui::About) {
  ui->setupUi(this);

  // init
  appName = QApplication::applicationDisplayName();
#ifdef Q_OS_WIN
  appDescription = "WhatsApp Web client for Windows Desktop"
                   "<br><span style=' font-size:8pt;'>Windows port — follows "
                   "the updates of the upstream project</span>";
#else
  appDescription = "WhatsApp Web client for Linux Desktop";
#endif
  isOpenSource = true;

  // Original author of WhatSie — kept credited: this project is an MIT-licensed
  // fork and the upstream authorship/copyright must be preserved.
  appAuthorName = "Keshav Bhatt";
  appAuthorEmail = "keshavnrj@gmail.com";
  appAuthorLink = "http://ktechpit.com";

  // Maintainer of this fork.
  maintainerName = "Ángel Guzmán Maeso";
  maintainerEmail = "angel@guzmanmaeso.com";
  maintainerLink = "https://shakaran.net";

  donateLink = "https://paypal.me/shakaran/5";
  moreAppsLink = "https://github.com/shakaran";
  reportBugLink = "https://github.com/shakaran/whatly/issues/new";

  appSourceCodeLink = "https://github.com/shakaran/whatly";
  appRateLink = "snap://whatsie";

  ui->appNameDesc->setText(
      QString("<p style=' margin-top:12px; margin-bottom:12px; margin-left:0px;"
              " margin-right:0px; -qt-block-indent:0; text-indent:0px;'>"
              "<span style=' font-size:18pt;'>%1</span></p>"
              "<p style=' margin-top:12px; margin-bottom:12px; margin-left:0px;"
              " margin-right:0px; -qt-block-indent:0; text-indent:0px;'>"
              "%2</p>")
          .arg(appName, appDescription));

  ui->desc2->setText(
      QString("<p><span style=' font-weight:600;'>Designed &amp; Developed "
              "by:</span>"
              " %1 </p><p><span style=' font-weight:600;'>Website:</span>"
              " %2</p>"
              "<p><span style=' font-weight:600;'>Fork maintained by:</span>"
              " %3</p><p><span style=' font-weight:600;'>"
              "Email: </span>%4</p>"
              "<p><span style=' font-weight:600;'>Website:</span>"
              " %5</p>")
          .arg(appAuthorName, appAuthorLink, maintainerName, maintainerEmail,
               maintainerLink));

  ui->version->setText(tr("Version: ") + QApplication::applicationVersion());

  ui->debugInfoText->setHtml(Utils::appDebugInfo());

  ui->debugInfoText->hide();

  ui->debugInfoButton->setText(QObject::tr("Show Debug Info"));

  if (isOpenSource == false) {
    ui->source_code->hide();
  }

  // The rate link points at the Snap store (snap://whatsie), which hosts the
  // upstream project, not this fork — hide it rather than send users elsewhere.
  ui->rate->hide();

  connect(ui->donate, &QPushButton::clicked,
          [=]() { QDesktopServices::openUrl(QUrl(donateLink)); });

  connect(ui->rate, &QPushButton::clicked,
          [=]() { QDesktopServices::openUrl(QUrl(appRateLink)); });
  connect(ui->more_apps, &QPushButton::clicked,
          [=]() { QDesktopServices::openUrl(QUrl(moreAppsLink)); });
  connect(ui->source_code, &QPushButton::clicked,
          [=]() { QDesktopServices::openUrl(QUrl(appSourceCodeLink)); });

  // Half of a useful bug report is which build it is and what the app was
  // saying at the time, and that is exactly what nobody can be bothered to
  // gather. GitHub takes the issue body as a query parameter, so it arrives
  // already filled in and the reporter only has to describe what happened.
  connect(ui->report_bug, &QPushButton::clicked, [=]() {
    const QString body =
        QObject::tr("<!-- What did you do, what did you expect, and what "
                    "happened instead? -->\n\n\n") +
        Utils::appDebugInfoMarkdown();

    QUrl url(reportBugLink);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("body"), body);
    url.setQuery(query);

    // A very long log makes a URL the browser or GitHub will refuse. Keep the
    // clipboard as the fallback so nothing is silently lost, and say so.
    if (url.toEncoded().size() > 7500) {
      QGuiApplication::clipboard()->setText(body);
      url.setQuery(QUrlQuery{});
      QMessageBox::information(
          this, tr("Report a Bug"),
          tr("The debug information was too long for the browser to carry, so "
             "it has been copied to your clipboard instead. Paste it into the "
             "issue."));
    }
    QDesktopServices::openUrl(url);
  });

  setWindowTitle(QApplication::applicationDisplayName() + tr(" | About"));

  ui->centerWidget->hide();

  QGraphicsOpacityEffect *eff = new QGraphicsOpacityEffect(this);
  ui->centerWidget->setGraphicsEffect(eff);
  QPropertyAnimation *a = new QPropertyAnimation(eff, "opacity");
  a->setDuration(1000);
  a->setStartValue(0);
  a->setEndValue(1);
  a->setEasingCurve(QEasingCurve::InCurve);
  a->start(QPropertyAnimation::DeleteWhenStopped);
  ui->centerWidget->show();
}

About::~About() { delete ui; }

void About::on_debugInfoButton_clicked() {
  if (ui->debugInfoText->isVisible()) {
    ui->debugInfoText->hide();
    ui->debugInfoButton->setText(QObject::tr("Show Debug Info"));

    this->resize(this->width(), this->minimumHeight());
  } else {
    ui->debugInfoText->show();
    ui->debugInfoButton->setText(QObject::tr("Hide Debug Info"));
    this->adjustSize();
  }
}

void About::keyPressEvent(QKeyEvent *e) {
  if (e->key() == Qt::Key_Escape)
    this->close();

  QWidget::keyPressEvent(e);
}
