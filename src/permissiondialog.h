#ifndef PERMISSIONDIALOG_H
#define PERMISSIONDIALOG_H

#include <QCheckBox>
#include <QKeyEvent>
#include <QMetaEnum>
#include <QWebEnginePermission>
#include <QWidget>

#include "settingsmanager.h"

namespace Ui {
class PermissionDialog;
}

class PermissionDialog : public QWidget {
  Q_OBJECT

public:
  explicit PermissionDialog(QWidget *parent = nullptr);
  ~PermissionDialog();

protected slots:
  void keyPressEvent(QKeyEvent *e);

private:
  void addPermissionRow(QWebEnginePermission::PermissionType type,
                        const QString &name);
  // Reads the effective state: the engine's own record for permissions it can
  // persist, and our stored answer for the rest.
  bool isGranted(QWebEnginePermission::PermissionType type) const;
  void setGranted(QWebEnginePermission::PermissionType type, bool granted);

  Ui::PermissionDialog *ui;
};

#endif // PERMISSIONDIALOG_H
