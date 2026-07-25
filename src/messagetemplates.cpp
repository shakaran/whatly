#include "messagetemplates.h"
#include "settingsmanager.h"

#include <QSettings>

namespace {
const char kNamesKey[] = "messageTemplates/names";
const char kBodiesKey[] = "messageTemplates/bodies";
} // namespace

namespace MessageTemplates {

QList<Template> all() {
  QSettings &s = SettingsManager::instance().settings();
  const QStringList names = s.value(QLatin1String(kNamesKey)).toStringList();
  const QStringList bodies = s.value(QLatin1String(kBodiesKey)).toStringList();
  QList<Template> out;
  for (int i = 0; i < names.size() && i < bodies.size(); ++i)
    if (!names.at(i).trimmed().isEmpty())
      out.append({names.at(i), bodies.at(i)});
  return out;
}

void setAll(const QList<Template> &templates) {
  QStringList names, bodies;
  for (const Template &t : templates) {
    names << t.name;
    bodies << t.body;
  }
  QSettings &s = SettingsManager::instance().settings();
  s.setValue(QLatin1String(kNamesKey), names);
  s.setValue(QLatin1String(kBodiesKey), bodies);
}

bool exists(const QString &name) {
  const QList<Template> list = all();
  for (const Template &t : list)
    if (t.name == name)
      return true;
  return false;
}

QString body(const QString &name) {
  const QList<Template> list = all();
  for (const Template &t : list)
    if (t.name == name)
      return t.body;
  return QString();
}

void set(const QString &name, const QString &body) {
  const QString n = name.trimmed();
  if (n.isEmpty())
    return;
  QList<Template> list = all();
  for (Template &t : list)
    if (t.name == n) {
      t.body = body;
      setAll(list);
      return;
    }
  list.append({n, body});
  setAll(list);
}

bool remove(const QString &name) {
  QList<Template> list = all();
  for (int i = 0; i < list.size(); ++i)
    if (list.at(i).name == name) {
      list.removeAt(i);
      setAll(list);
      return true;
    }
  return false;
}

} // namespace MessageTemplates
