#include "permissiondialog.h"
#include "ui_permissiondialog.h"

#include "common.h"
#include "webengineprofilemanager.h"

#include <QUrl>

// The key handlePermissionRequested() reads: the numeric PermissionType.
static QString settingsKey(QWebEnginePermission::PermissionType type) {
  return QStringLiteral("permissions/%1").arg(static_cast<int>(type));
}

PermissionDialog::PermissionDialog(QWidget *parent)
    : QWidget(parent), ui(new Ui::PermissionDialog) {
  ui->setupUi(this);

  ui->featuresTableWidget->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Stretch);
  ui->featuresTableWidget->verticalHeader()->setVisible(true);
  ui->featuresTableWidget->horizontalHeader()->setVisible(true);
  ui->featuresTableWidget->setSelectionMode(QAbstractItemView::NoSelection);
  ui->featuresTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui->featuresTableWidget->setFocusPolicy(Qt::NoFocus);

  // Walk QWebEnginePermission::PermissionType — the enum the permission handler
  // actually keys on. The dialog used to walk the deprecated
  // QWebEnginePage::Feature enum, which numbers its values completely
  // differently (Notifications is 0 there and 7 here), so every row it drew
  // referred to a different permission than the one it claimed to.
  const QMetaEnum meta =
      QMetaEnum::fromType<QWebEnginePermission::PermissionType>();
  for (int i = 0; i < meta.keyCount(); i++) {
    const auto type =
        static_cast<QWebEnginePermission::PermissionType>(meta.value(i));
    if (type == QWebEnginePermission::PermissionType::Unsupported)
      continue;
    addPermissionRow(type, QString::fromLatin1(meta.key(i)));
  }
}

bool PermissionDialog::isGranted(
    QWebEnginePermission::PermissionType type) const {
  if (QWebEnginePermission::isPersistent(type)) {
    const QWebEnginePermission permission =
        WebEngineProfileManager::instance().profile()->queryPermission(QUrl(whatsAppOrigin), type);
    if (permission.isValid())
      return permission.state() == QWebEnginePermission::State::Granted;
  }
  return SettingsManager::instance()
      .settings()
      .value(settingsKey(type), false)
      .toBool();
}

void PermissionDialog::setGranted(QWebEnginePermission::PermissionType type,
                                  bool granted) {
  // Remember the answer for the next request...
  SettingsManager::instance().settings().setValue(settingsKey(type), granted);

  // ...and, where the engine can hold the decision itself, apply it right now.
  // This is what makes the toggle actually do something: once a permission has
  // been denied, the page sees "denied" forever and never asks again, so there
  // was no way back — WhatsApp Web would just keep showing "Message
  // notifications are off" with instructions for a browser address bar that
  // does not exist here.
  if (!QWebEnginePermission::isPersistent(type))
    return;

  const QWebEnginePermission permission =
      WebEngineProfileManager::instance().profile()->queryPermission(QUrl(whatsAppOrigin), type);
  if (!permission.isValid())
    return;
  if (granted)
    permission.grant();
  else
    permission.deny();
}

void PermissionDialog::addPermissionRow(
    QWebEnginePermission::PermissionType type, const QString &name) {
  if (name.isEmpty())
    return;

  const int row = ui->featuresTableWidget->rowCount();
  ui->featuresTableWidget->insertRow(row);

  auto *item = new QTableWidgetItem(name);
  item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
  ui->featuresTableWidget->setItem(row, 0, item);

  auto *checkBox = new QCheckBox(nullptr);
  checkBox->setStyleSheet("border:0px;margin-left:50%; margin-right:50%;");
  checkBox->setChecked(isGranted(type));
  connect(checkBox, &QCheckBox::toggled, this,
          [this, type](bool checked) { setGranted(type, checked); });
  ui->featuresTableWidget->setCellWidget(row, 1, checkBox);
}

void PermissionDialog::keyPressEvent(QKeyEvent *e) {
  if (e->key() == Qt::Key_Escape)
    this->close();

  QWidget::keyPressEvent(e);
}

PermissionDialog::~PermissionDialog() { delete ui; }
