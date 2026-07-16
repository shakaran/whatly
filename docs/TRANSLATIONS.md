# Translating Whatly

Whatly's interface can be translated. The language follows the system locale by
default and can be overridden in **Settings → General settings → Interface
language**; the change takes effect after a restart.

> **What this does *not* cover.** Only Whatly's own interface — menus, settings,
> dialogs — is translated here. The language of the chats is WhatsApp Web's, and
> WhatsApp picks it from your account and browser locale. Nothing in this project
> can change it.

## ⚠️ Most of these translations are machine-generated

`it_IT` was contributed by a human translator. **Every other language was
generated without native-speaker review** and is very likely to contain awkward
or plainly wrong wording — more so the further it gets from the Western European
languages. They are here because an untranslated interface helps nobody, not
because they are good.

**Corrections from native speakers are the whole point.** Fixing even a handful
of strings in your language is a genuinely useful contribution, and you do not
need to know C++ to do it.

## How the pieces fit together

| | |
|---|---|
| `src/i18n/<locale>.ts` | The translations, one XML file per locale (Qt Linguist format) |
| `CMakeLists.txt` | Lists the `.ts` files; CMake compiles them to `.qm` and embeds them in the binary |
| `src/main.cpp` | `installTranslations()` loads `:/i18n/<locale>.qm` for the chosen or system locale |
| `SettingsWidget::populateLanguages()` | Builds the picker by listing the embedded `.qm` files |

Because the picker is built from the embedded files, **adding a language needs no
code change** beyond listing the new `.ts` in `CMakeLists.txt`.

## Fixing or improving a translation

Edit `src/i18n/<locale>.ts` — either in a text editor or with **Qt Linguist**,
which is friendlier:

```bash
sudo apt install qt6-tools-dev-tools   # provides linguist, lupdate, lrelease
linguist src/i18n/es_ES.ts
```

Then rebuild and check it in the app. Only translations marked *finished* are
compiled in; an empty or unfinished one falls back to English, which is why the
odd untranslatable string (`Form`, `-`, the About dialog's rich-text blob, all of
which are Qt Designer artefacts or overwritten at runtime) is simply left alone.

## Adding a new language

```bash
# 1. Create the file for your locale
/usr/lib/qt6/bin/lupdate src/ -ts src/i18n/xx_XX.ts -no-obsolete

# 2. Translate it
linguist src/i18n/xx_XX.ts

# 3. Add it to the TS_FILES list in CMakeLists.txt, then rebuild
```

It will show up in the language picker automatically, named in its own language.

## Keeping translations in sync with the code

After changing any translatable string in the source, refresh every `.ts` so the
new strings appear (existing translations are preserved):

```bash
/usr/lib/qt6/bin/lupdate src/ -ts src/i18n/*.ts -no-obsolete
```

## Building without the Linguist tools

They are optional. Without them CMake prints a warning, skips the translations
and the interface stays English — the build still succeeds.
