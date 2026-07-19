#include "customjs.h"
#include "settingsmanager.h"
#include "appprofile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

static const char kScriptName[] = "whatly-custom-js";

namespace {

QString addonsDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         QStringLiteral("/jsaddons") + AppProfile::suffix();
}

QString addonPath(const QString &name) {
  return addonsDir() + QLatin1Char('/') + name + QStringLiteral(".js");
}

// Restrict names to a safe, filename-friendly set so they can't escape the dir.
QString sanitize(const QString &raw) {
  QString out;
  out.reserve(raw.size());
  for (const QChar c : raw) {
    if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_'))
      out.append(c);
    else if (c == QLatin1Char(' ') || c == QLatin1Char('.'))
      out.append(QLatin1Char('-'));
  }
  while (out.startsWith(QLatin1Char('-')) || out.startsWith(QLatin1Char('.')))
    out.remove(0, 1);
  return out.left(64);
}

QString enabledKey(const QString &name) {
  return QStringLiteral("jsAddon/") + name + QStringLiteral("/enabled");
}

} // namespace

namespace CustomJs {

QList<Addon> addons() {
  QList<Addon> list;
  QDir dir(addonsDir());
  const auto files =
      dir.entryInfoList({QStringLiteral("*.js")}, QDir::Files, QDir::Name);
  for (const QFileInfo &fi : files)
    list.append({fi.completeBaseName(), isEnabled(fi.completeBaseName())});
  return list;
}

QString sourceOf(const QString &name) {
  QFile f(addonPath(name));
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return QString();
  return QString::fromUtf8(f.readAll());
}

QString addFromFile(const QString &path, QString *error, const QString &name) {
  QFile in(path);
  if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error)
      *error = in.errorString();
    return QString();
  }
  const QByteArray data = in.readAll();
  in.close();

  QString base = sanitize(name.isEmpty() ? QFileInfo(path).completeBaseName()
                                         : name);
  if (base.isEmpty())
    base = QStringLiteral("addon");

  const QString dir = addonsDir();
  if (!QDir().mkpath(dir)) {
    if (error)
      *error = QObject::tr("Cannot create %1").arg(dir);
    return QString();
  }

  // Avoid clobbering an existing addon of the same name: suffix -2, -3, ...
  QString finalName = base;
  for (int i = 2; QFile::exists(addonPath(finalName)); ++i)
    finalName = base + QLatin1Char('-') + QString::number(i);

  QFile out(addonPath(finalName));
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error)
      *error = out.errorString();
    return QString();
  }
  out.write(data);
  out.close();

  setEnabled(finalName, true);
  return finalName;
}

void remove(const QString &name) {
  QFile::remove(addonPath(name));
  SettingsManager::instance().settings().remove(enabledKey(name));
}

void setEnabled(const QString &name, bool enabled) {
  SettingsManager::instance().settings().setValue(enabledKey(name), enabled);
}

bool isEnabled(const QString &name) {
  return SettingsManager::instance()
      .settings()
      .value(enabledKey(name), true)
      .toBool();
}

bool isActive() {
  const auto list = addons();
  for (const Addon &a : list)
    if (a.enabled && !sourceOf(a.name).trimmed().isEmpty())
      return true;
  return false;
}

QString scriptSource() {
  QString combined = QStringLiteral("(function(){'use strict';\n");
  const auto list = addons();
  for (const Addon &a : list) {
    if (!a.enabled)
      continue;
    const QString src = sourceOf(a.name);
    if (src.trimmed().isEmpty())
      continue;
    // Each addon in its own guarded scope so one failure never breaks the rest.
    combined += QStringLiteral("try{\n") + src +
                QStringLiteral("\n}catch(e){/* addon: ") + a.name +
                QStringLiteral(" */}\n");
  }
  combined += QStringLiteral("})();\n");
  return combined;
}

void install(QWebEngineProfile *profile) {
  auto *scripts = profile->scripts();
  const auto existing = scripts->find(QLatin1String(kScriptName));
  for (const auto &script : existing)
    scripts->remove(script);

  if (!isActive())
    return;

  QWebEngineScript script;
  script.setName(QLatin1String(kScriptName));
  script.setSourceCode(scriptSource());
  script.setInjectionPoint(QWebEngineScript::DocumentReady);
  script.setWorldId(QWebEngineScript::MainWorld);
  script.setRunsOnSubFrames(false);
  scripts->insert(script);
}

} // namespace CustomJs
