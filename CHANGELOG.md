## Unreleased

- **Scheduled messages work again whatever language WhatsApp Web is in.** The sender waits for the chat to open and then presses WhatsApp's own Send button, which it found by `data-icon="send"`. WhatsApp renamed that icon to `wds-ic-send-filled` and no longer puts `send` on the page at all, so the lookup fell through to its last resort: two hardcoded labels, `aria-label="Send"` and `aria-label="Enviar"`. In any other interface language nothing matched, the sender kept retrying until its own 45-second deadline, and the message was reported as failed with "timeout waiting for the chat to open" — blaming the chat, which had opened perfectly well. The current icon name is now tried first, with the old one and the labels kept behind it. The same rename had already been handled where the composer's Send is pressed for a dropped attachment; this second copy of the lookup was missed.
- **"Send photos and videos in HD by default" now actually sends in HD, and no longer opens a dialog for every small image (#96).** WhatsApp Web's media editor names its own icons inside the icon element, and the quality control's name (`wds-ic-hd-settings`, on a wrapper that also carries it as `data-testid`) is neither translated nor changed by the attachment. Its *label* is both: "Photo quality" for media that can go HD, and the reason it cannot ("This media is not HD resolution.") for media that cannot. Whatly looked for the word "HD" in that label — so it found this control only in the state where clicking it can do nothing at all. The result was the exact inverse of the setting: a photo that could be sent in HD was never touched, while every small image cost a "Cannot set to HD" dialog to dismiss, one per attachment, for ever. The control is now found by the icon's name, and whether HD is possible is settled before anything is clicked, so that dialog is not provoked at all: WhatsApp dims the icon when the attachment cannot take HD, and the dimming is what is read — the control is otherwise identical in both states, down to reporting `aria-disabled="false"` while refusing, and the preview's own pixel size describes the quality already chosen rather than what the file could do. Because the dimming comes from a generated class name that changes between WhatsApp deployments, the rendered opacity is read and never the name; and should WhatsApp stop dimming it altogether, a refusal that gets through is remembered by its wording — which the dialog repeats, so it is recognised in any language — instead of being provoked again for every attachment. A quality entry is also only taken from outside the conversation, so a message that merely says "HD" cannot be clicked in place of the menu.
- **A second window being put away no longer delays the first one's unload.** When a window has been off screen for the configured delay, the accounts in it are unloaded to give back their memory. Two things drive that: a single-shot timer armed when a window goes, and a minute-long sweep behind it. There is one timer but a wait per window, and the timer was armed with a fresh full delay every time any window went away — so it always belonged to whichever window went last. Hide window A, then hide window B a minute later, and B's arming moved the timer a minute past A's deadline; once the timer had been spent, a window still short of its own wait got no replacement at all. It is now armed for the earliest deadline still to come, read from the per-window stamps that were already kept, and re-armed after each timeout for the next one. Nothing was ever lost to this — the sweep unloads any window that is past its wait regardless, which is why the effect was an unload up to a minute late rather than one that never came — but on a one-minute delay that minute is the whole setting.
- **Wheel scrolling no longer waits on the page's main thread.** Every page load installed a `wheel` listener on `window` registered `passive: false`, to stop Ctrl+wheel from zooming. A non-passive wheel listener spanning the whole document is the one thing that takes scrolling off Chromium's compositor thread: the compositor cannot know whether the handler will cancel the event, so it has to hand each wheel tick to the main thread and wait for the answer before moving a pixel. The zoom is now blocked in C++ instead, on the internal render widget, which is the only object a wheel event actually reaches; the existing `wheelEvent()` override on the view was never called, which is what sent the fix into the page in the first place. Ctrl+wheel still neither zooms nor scrolls, and the page is left with no wheel handler of Whatly's at all. **Measured on WhatsApp Web, worst-case wheel-to-scroll latency fell from 46.9 ms to 18.6-22.1 ms across runs.** To be clear about what this does not do: the same measurements show no improvement in frame pacing (about a quarter to a third of frames miss a 30 fps deadline either way) and none in the large backwards jumps that happen when the message list re-anchors after loading history. Those come from the page itself, not from this handler.
- **On Windows, a spell-check language could show as installed and check nothing at all.** The bundled dictionaries are mirrored into a writable directory so they merge with any the user downloaded, and that mirror was made with `QFile::link()`. On Windows that call does not make a link: it writes a `.lnk` shortcut, and Qt's own documentation says the name has to end in `.lnk` for it to be a link at all — this one ends in `.bdic`. So what landed beside the real dictionaries was a 1.5 KB shortcut wearing a dictionary's name. Chromium cannot read it, and neither could the cleanup meant to remove it: `QFileInfo::isSymLink()` decides by file name on Windows, so the shortcut was not a link either, and the check for a link whose target had gone never saw it. It stayed for good, the language sat in the list looking installed, and nothing it was asked to check was ever checked. Two changes. The cleanup now asks the file what it is — a `.bdic` begins with `BDic` — instead of asking how it got there, which also removes a half-written download and still catches the case it was written for, an AppImage whose links no longer resolve because it remounted somewhere else. And on Windows the mirror is a copy, the only form the engine can open. The same wrong question was being asked in two more places: whether a dictionary can be deleted, and whether the list should call it "Shipped with Whatly". Both now ask whether the build bundles that language, which is what they always meant and is true on either platform. Existing profiles repair themselves on the next launch.

## 7.3.1 (2026-08-23)

- **Test coverage raised to ~90%.** New headless suites drive the parts the pure-logic tests could not reach: the network clients (Ollama check/pull, the translator and the AI assistant) against a stand-in HTTP server, including their no-endpoint, empty-input, malformed-reply and network-error paths; the dictionary catalogue fetch and asset download, the Cloud API send path (text/template/media, with their API-error, network-error and unconfigured branches), and the update check (available/up-to-date/malformed/network-error), each pointed at a local stand-in through an env override; the local API server end to end over loopback (auth, routing, `/send`, and the `/webhook` GET handshake and POST delivery/signature); the widgets offscreen — the command palette, the account tab bar as click and drop target, and the dictionary-row delegate across its states; the tar-based profile export/import and its error paths; the drop reader and resolver; scheduled-message due-checking, recurrence and persistence; session-backup startup recovery; chat-export formatting; the About dialog's toggle and key handling; and assorted `Utils` helpers. What little stays uncovered is code a headless CI cannot exercise — the D-Bus notification and portal paths, the tear-off drag, and GUI-only branches.
- **The Cloud API host and the update-check URL can be overridden by an environment variable.** `WHATLY_CLOUD_API_BASE` replaces the Graph API host and `WHATLY_UPDATE_URL` the GitHub releases endpoint (as `WHATLY_DICT_BASE_URL` already does for the dictionary catalogue). Both default to the real service when unset; they exist so the network paths can be pointed at a local stand-in for testing, and incidentally let an advanced user route through a proxy.
- **Codecov coverage reporting.** A coverage workflow builds the unit suites instrumented for gcov, runs them headless, and uploads a Cobertura report to Codecov; a badge in the README tracks it. (Coverage reflects the CI-safe headless suites — the pure logic in `src/` plus the offscreen widget and network-client tests — not WhatsApp Web itself.)
- **OpenSSF Scorecard.** A weekly workflow audits the repository's supply-chain security posture and publishes the result; the score is shown as a badge in the README and its detail at scorecard.dev, with findings also in the code-scanning tab.
- **No more "OpenType support missing for Noto Sans" log spam.** The language picker lists each language by its native name, so Qt renders scripts the default sans family does not itself carry (Devanagari, Bengali, Arabic…) and logged a warning per script — even though it composes them correctly from the per-script Noto families (fallback working as designed). That one category is now quieted, appended to any `QT_LOGGING_RULES` the user already set.
- **The NVIDIA-on-Wayland fix no longer forces XCB on setups where Wayland works (#84).** 7.3.0 switched every proprietary-NVIDIA Wayland session to XCB up front to avoid a blank window, but on a Wayland setup that renders fine that sent it onto XWayland, where the NVIDIA GL stack could crash the renderer in a loop. Whatly now stays on Wayland and only relaunches on XCB once, automatically, if the backing store actually fails to get a renderer — so the blank-window case is still covered and a working Wayland session is left alone.
- **The "storage bucket persistence denied" console error is gone.** WhatsApp Web moved to the Storage Buckets API, which the existing `navigator.storage.persist()` override did not cover, so the persistence grant Whatly fakes for the classic API was denied for the new one. The override now also wraps `navigator.storageBuckets.open()` so a bucket reports itself persisted.
- **The Windows/macOS codec notice no longer points at a package that does not exist (#93).** When a build without the proprietary H.264 codecs meets an MP4, the notice named "a distro/native package built with the codecs" — but off Linux there is no Flatpak and no codec-enabled build of Whatly at all, so that remedy pointed at nothing. It now says plainly that this build cannot do MP4, that photos and WebM/VP9 work, and offers a real way through: convert the MP4 to WebM, or send it as a document. (Shipping a codec-enabled Windows build is a separate, licensing-gated decision and is not this change.)

## 7.3.0 (2026-08-15)

- **Two more interface languages: Bengali and Urdu, for 22 in total.** The two most widely spoken languages Whatly did not yet have, both with large WhatsApp user bases. Urdu is right-to-left, like the Arabic and Persian already supported. Pick either from Settings, or leave the interface on the system language.

- **The Windows build compiles again.** `main.cpp` includes `<windows.h>` for the restart handshake, and `windows.h` defines `min()` and `max()` as macros. Every Qt header pulled in after it that writes `std::max(...)` is then handed a mangled token: MSVC died inside `QtQml/qjslist.h`, a file nobody here wrote, with `error C2589: '(' : illegal token on right side of '::'`. The include now carries `WIN32_LEAN_AND_MEAN` and `NOMINMAX` before it, as `lock.cpp` has done all along. Nothing else changes; the failure was latent from the day the include was added and only surfaced when the include chain grew as far as QtQml.

- **The "WhatsApp Web did not finish loading" notice no longer fires on a load that succeeds (#43).** WhatsApp Web prints `Requiring module "…" with unresolved dependencies: … is waiting for WAWebUserPrefsGeneral / WAWebUserPrefsGeneral is not defined` while it is still resolving its modules, and then loads normally seconds later. That carries both phrases the detector looked for, so every launch logged a failure notice — and advised clearing the profile data — for a page that was about to come up. The real collapse ends by naming a numeric Chromium module id it cannot resolve (`cr:34987 is not defined`), which is what the code comment always described; the detector now requires that id. A named module that is merely late is ignored.
- **Settings has a search box (#39).** Fourteen sections and something past a hundred and fifty controls: knowing a setting exists stopped being enough to find it. Typing in the box at the top of the page filters it down to the settings that match — and what is left standing is the setting itself, live and connected where it always was, not a list of results to click through: tick it, type in it, change it, exactly as if you had found it by opening sections one at a time. That is how VLC's preferences search behaves, which is the shape this was asked for in. A section whose own title matches shows everything in it; sections with nothing in them are folded away entirely, and clearing the box gives the page back exactly as it was, including which sections were open before you started typing. Escape clears the search rather than closing the page. Tooltips are searched as well as labels, because that is where the words people actually search with live — the label says "unload inactive accounts" and only the tooltip says "memory" — and matching ignores case, accents and hyphens, so "basico" finds "Básico" and "spellcheck" finds "spell-check". With Whatly running in another language the English names are searched too: a .qm file maps English to the local language and cannot be read backwards, so the form's own English is read off the page once at startup, while it is still on screen, and kept for the search to match against.
- **The interface languages are named the way the spell-check languages are.** The two lists sit a few sections apart and are read together — the language Whatly speaks, and the languages it checks spelling in — and they called the same language different things: "Español de España (es_ES)" against "español (España)". Both now come from one routine, which names each language in itself, qualifies it by territory where CLDR's default would otherwise mislead, and falls back to the bare code for anything Qt does not model. It also fixes a name that was simply wrong: built from the language alone, zh_TW threw its territory away and resolved to zh_CN's, so the traditional-Chinese translation was offered as "简体中文" — simplified. A test walks the shipped catalogues and fails if any two of them read the same.
- **The AppImage can update itself in place (#85).** The image already carries its own zsync update information and the app already said it could update by delta — but nothing ran the update, so in practice people re-downloaded the whole ~150 MB. When `appimageupdatetool` is present, the update notice now offers **Update now**, which fetches only the changed blocks over HTTPS from the GitHub release and offers to restart when it is done. It appears only for the AppImage with the tool installed; Flatpak and distribution packages are untouched (they update from outside the app), and nothing is shown when the tool is absent. Payload signing is a separate, later step.

- **On proprietary NVIDIA under Wayland, the window no longer comes up blank (#84).** That driver often ships no working `wayland-egl`, so Qt cannot get a renderer for the window's backing store and it opens blank and unfocusable — with no way to reach Settings to change anything. Whatly now detects that exact combination at start-up and uses the XCB (XWayland) platform, which has GLX and works there. Only that case is redirected, and only when you have not chosen a platform yourself: a working NVIDIA-on-Wayland setup can force native Wayland back with `QT_QPA_PLATFORM=wayland`.

- **In dark mode, a greyed-out setting looked exactly like a live one.** Every colour the dark palette sets was set for all three colour groups at once — `QPalette::setColor(role, colour)` does that — so "window text is white" made *disabled* window text white as well. A check box's label is drawn in that role, and the light palette has always set its disabled colour explicitly while the dark one never did: in Settings, where a box is greyed out until the setting it depends on is switched on, it read as available and then did nothing when clicked. The dark palette now sets it, to the same grey as the disabled colours next to it. Found in the Performance section, but it applies to every disabled label in the interface.
- **The state Settings keeps for "Restart now" is consumed when it is used, and ignored after an upgrade.** That state — which sections are open, how far the page is scrolled — exists so a restart-only setting can be changed without losing your place, and it was already written by nothing but the restart button and read only by the launch that followed it. Two gaps are now closed. It is deleted the moment it is replayed, so no launch that never asked for it can find anything to replay. And it carries the version that wrote it: the sections are stored by their position in the accordion, which is only a name for the same section while the accordion is the same, so a version that adds a section or reorders them would have opened the wrong rows. The window's size and place are still restored whatever wrote them, because those mean the same thing in any version.
- **A media download that never arrives now says so, and says what to try.** Click the arrow on a photo, video or sticker that WhatsApp cannot give you and nothing happens at all: the placeholder stays, no message appears anywhere, and the reason is invisible even in the log — a refused download is a network-level failure, and QtWebEngine hands the application only JavaScript console messages. What can be known without any of that is the part that matters, that the same file was asked for twice and still has not come. The second click now starts a short wait, and if the placeholder is still a placeholder afterwards, a dismissable notice says the file did not download and that it will have to be asked for again from the phone that sent it — WhatsApp keeps media on its servers for a while only, and an old message's copy is usually gone. When the connection is what is missing the notice says that instead, and promises the file will arrive by itself, because that much the app can know for certain. One notice, where the user is looking, and a line in the log for the bug report.

- **The tray badge counts past ten, and says the real number in its tooltip.** The count was clamped at ten, and in colour mode everything from ten upwards wore one file — `whatly-notify-10.png`, a bare "+" with no digit in it — while monochrome mode drew "9+": two modes disagreeing about the same inbox, and the one most people run carrying less information. The clamp was harmless while the count came from the window title and under-reported wildly (it once said "1" for 55 unread messages), but a real count of unread chats sits above ten permanently, so the badge stopped saying anything at all. It now draws the number up to 99 and "99+" beyond, in both modes, from one routine: the artwork for one to nine is untouched, and past nine the badge is drawn over the plain icon in the same red, widening for the second and third character. Digits stop at two because a panel draws the whole icon at some 22 px, where a third digit is about three pixels wide — so the tray icon now also has a tooltip, and it carries more than the badge ever could: the unread messages and the chats they are waiting in, split into muted and not muted where the page can tell the two apart. In the monochrome icon the badge is grey with white digits rather than the glyph's own light tone, which merged with the glyph into one bright blob and left the digits as the only thing to read.
- **The log says which WhatsApp Web build each account is running.** WhatsApp Web updates itself under the app and says nothing about it, so a page that started misbehaving could not be tied to a version afterwards — the version was read on load already, but only to label the account's tab, and only when it changed. It is now written to the log on every load, per account, and names the previous build when it has moved.
- **One list for the spell-check languages: tick it, get it, get rid of it (#46).** Choosing a language to check spelling against and getting hold of its dictionary were two different places — the picker beside "Check spelling as I type", and a separate "Spell-check dictionaries" section which in fact never appeared where it was meant to: its group was never added to the settings accordion, so all that showed of it was its title, floating over the top of the page. There is one list now, the picker, and every language has a row in it. A language you have is a tick box with a bin beside it; one you have not is greyed, with the size it will cost and a download arrow; a bundled one says "bundled" and has no bin, because deleting a link into the read-only bundle would only relink on the next start. A download shows its per-cent on its own row, arrives ticked and works without a restart, and the list stays live with the checker switched off — whoever has no dictionaries at all has to be able to fetch one from somewhere. Hovering a row says what is known about that dictionary: its size, when it arrived, and the code Chromium is given. It deliberately says nothing more, since the manifest carries a code, a size and a hash, so a version or an upstream date would have to be invented. While a dictionary is on its way its row spins where its tick box belongs, and the tick box takes that place back — ticked — the moment the file lands. And when the list of downloadable languages cannot be fetched at all, which happens (the fetch follows a GitHub release redirect, and an HTTP/2 stream refused on the way through it is enough), it is tried twice more and then said on a row of its own that is also the retry: before that, a failed fetch left a list of the one or two installed languages looking exactly like a list of every language there is.

- **A language is named after its territory once, not twice.** CLDR qualifies the variants that are not its default, and this list names the territory itself, so Portuguese came out as "português europeu (Portugal)" — the same thing said twice, standing out in a list where every other row says it once. The qualifier is now dropped when it is one: "português (Portugal)" beside "português (Brasil)", as "español (España)" already read beside "español (México)". A name that differs outright rather than by a qualifier — "British English" beside "American English" — is left exactly as CLDR has it.
- **An account can now be unloaded with the window it is in (#25).** "Unload inactive accounts from memory" only ever applied to the accounts you were not looking at: the one on screen kept its renderer whatever became of its window, so putting Whatly away to the tray — where it spends most of its day — gave nothing back. A new box under it, "Unload also minimised and hidden accounts", extends the same rule to whole windows: once a window has been minimised or put away for the delay set above, the account it was showing goes as well, and it is built again the moment the window comes back. The same delay, counted from when the window went rather than from when the account was last read — the account on screen was being read until the window went, so its own idle clock would say "just now" for a window that has been gone all afternoon. Each window is judged on its own, so an account torn out into a window of its own is not unloaded because the main window went away; and every unload and reload writes a line to the log, because from the outside a page coming back looks like WhatsApp Web reloading for no reason. It needs the setting above it and is off by default, and for a plain reason: nothing at all reaches an unloaded account, so while its window is away that account raises no notifications and its counts stand still.
- **A wheel scrolling the Settings page can no longer set the interface scale or the page zoom.** The guard that keeps the wheel from changing a control it merely passes over was installed on `QSpinBox`, and the interface scale and the two zoom factors are `QDoubleSpinBox` — a sibling of `QSpinBox`, not a subclass — so those three were never covered. The interface scale is the one that does damage: it is only read at launch, so the setting is changed silently and the app comes back at half size, with nothing left to connect the tiny window to the scroll that caused it. The guard now covers every `QAbstractSpinBox`, and the wheel regression test checks the interface scale by name.

## 7.2.2 (2026-08-13)

- **The "update available" notification is clickable again on KDE/Linux (#74).**
  It was the only notification that went through the system tray toast, which
  registers no action with the freedesktop server, so clicking it on KDE did
  nothing. It now goes through the same libnotify path as message notifications,
  with an "Open" action that opens the release page.
- **The spell-check language box opens on the first click (#75).** Since 7.2.0,
  clicking the box anywhere but its arrow made the list flash and vanish until
  you clicked the arrow once. The click is now handed to the arrow so Qt takes
  its own path, and the list opens and stays open every time.

## 7.2.1 (2026-08-13)

A packaging point release: no application changes, only how Windows updates ship.

**Windows: a small update patch (.msp) instead of the whole installer (#71).**
Because Qt is now pinned per minor version, a point release usually changes only
the app — about 1% of the ~140 MB Windows download. Each x.y.(>0) release now
also ships a `whatly-<version>-x64.msp` built against that minor's x.y.0
installer, so an existing install can be updated by applying a couple of
megabytes rather than re-downloading everything. The full `.msi` and portable
`.zip` are still published for fresh installs; the patch is an extra. (AppImage
and Flatpak already update by delta.)

## 7.2.0 (2026-08-12)

Whatly 7.2.0 builds on 7.1.0. Highlights, all local and private: one-click AI
tone rewrites of your draft (more formal, friendlier, shorter), an AI digest of
your unread chats, and reply reminders that reopen a chat when they come due —
all running against your configured endpoint, so they work fully offline with
Ollama. Do Not Disturb now has an on-demand toggle with quick durations, and the
spell checker can switch its active language mid-sentence. Under the hood, a
linked account survives a corrupt or wiped profile without re-linking (automatic
session backup + Service Worker recovery), and a low-disk warning offers to move
the data folder before a full disk can corrupt it. Plus a stack of packaging
fixes: the Windows installer and portable zip now carry the MSVC runtime, the
update notice advises correctly per installation type, and the Linux .deb/.rpm
ship all their icons and AppStream data.

**The .deb and portable .rpm now show their icons and appear in software centres (#67).** The packaging staged the whole app under /opt/whatly and copied only the 256x256 icon back to a system path, so six of the seven icons — including the monochrome tray icon — were invisible to the desktop, and the AppStream metainfo never reached /usr/share/metainfo, leaving the app with no entry in GNOME Software or KDE Discover. The full icon set, the metainfo and the licence now go to the right system paths. Separately, the native openSUSE .rpm was shipping ~54 MB of unstripped debug info (almost the whole download); its binary is now stripped, taking that package from ~17.5 MB to ~3 MB. The portable .rpm is also renamed **whatly-bundle** (it bundles Qt under /opt), so it can no longer be mistaken by rpm for an upgrade of the native openSUSE **whatly** package and silently switch a user to the bundled layout.

**Switch spell-check language mid-sentence.** Ticking three
languages in Settings means Chromium accepts any word that any of the three
knows, so most typos in the language you are actually writing go unmarked. A new
**Spelling** submenu in the tray icon — and **Ctrl+Alt+S**, rebindable in
Settings like every other shortcut — now switches between the languages you
chose, checking against one at a time, and comes back round to all of them at
once for a message that mixes two. The switch takes effect on the page you are
typing on, with no reload, and says which language it landed on; what you ticked
in Settings is left alone, since that is the set being switched between. Two
things about the language box itself came out of dogfooding it: it now opens its
list when clicked anywhere on the box rather than only on the arrow, and an open
Settings page keeps up with a language switched from the tray or the keyboard
instead of going on claiming "3 languages" while one of them does the work. The
languages are also named properly at last, and named alike: every English
dictionary read "American English" and pt_PT was indistinguishable from pt_BR,
because the name was built from the language with its territory thrown away. Each
entry now reads as its own language plus the place it belongs to — "British English
(United Kingdom)", "português (Brasil)", "español (España)", "español (Argentina)" —
one shape for the whole list.

**The update notice now says the right thing for the way you installed Whatly.**
It told everyone alike to click through to the download page, which is wrong
advice for most installations: a Flatpak cannot replace itself from inside its
sandbox, and a package from the AUR, from OBS or from a distribution repository
is updated by the package manager, not by hand. Each now gets its own sentence —
Flathub or your software centre, your package manager, or the download page — and
a running AppImage is told it can patch itself in place with AppImageUpdate,
fetching only the parts that changed rather than the whole 150 MB. The notice
still opens the release page when clicked, wherever the new version comes from,
because the notes are worth reading either way.

**The wheel in Settings scrolls what the pointer is on.** Opening the
spell-check language picker and turning the wheel over its list scrolled the
Settings page instead, leaving the list floating in mid-air over settings it had
nothing to do with. A wheel over a list now belongs to that list, the page
scrolls when the pointer is on the page, and whichever of the two the gesture
started on keeps it — so a scroll down the page no longer stops dead the moment
the pointer crosses a list.

**Windows: the installer and portable zip now run on a clean machine (#68).**
`whatly.exe` imports the MSVC runtime (`MSVCP140.dll`, `VCRUNTIME140*.dll`), but
neither Windows artifact shipped it, so a PC without the "VC++ 2015–2022
Redistributable" failed to launch with a missing-DLL dialog. The runtime is now
deployed next to the executable, fixing both the `.msi` and the `.zip`; a CI
guard fails the build if it is ever missing again.

**A warning before a full disk corrupts your session.** When the data folder's
disk drops below 1 GB free, Whatly now warns at startup that WhatsApp Web's
local database can be corrupted by a truncated write (which forces you to link
your phone again) and offers to **move the data folder to a roomier disk** right
from the warning; the new location applies after a restart. Relatedly, Whatly's
automatic session backup now backs off when free space is under 512 MB instead
of spending the last bytes on a copy that would itself be corrupt, and each
`--profile` instance keeps its own snapshot namespace.

**Settings sections styled correctly in every language.** The collapsible
Settings sections were named after their translated titles, and a name with an
accent or a slash — "Básico", "IA/traducción" — is not a valid Qt style-sheet
selector, so on non-English locales the outline was dropped and the log filled
with "Could not parse stylesheet" on every repaint. The sections are now named
by a locale-independent index, so the styling applies and the noise is gone.

**One-click tone rewrites with the local AI (idea #5).** Next to *Improve
message*, three new actions rewrite the message you are typing in a chosen tone:
**more formal**, **friendlier**, or **shorter** — keeping the meaning and the
language, and dropping the result straight back into the composer. Available
from the message box's right-click menu, the Ctrl+K command palette and the
Shortcuts, and, like every AI action, run against your configured endpoint so
they can be fully local (Ollama).

**Reply reminders: snooze a chat (idea).** On the chat you have open, set a
reminder to reply — **in 1 hour**, **in 3 hours**, or **tomorrow morning** —
from the right-click menu, the Ctrl+K palette or the Shortcuts. When it comes
due Whatly raises a desktop notification and reopens that chat, so a
conversation you meant to get back to does not slip away. The reminder is
persisted, so it still fires after a restart.

**Do Not Disturb on demand (idea #10).** Beyond the scheduled quiet-hours
window, you can now silence notification popups right now: a checkable **Do Not
Disturb** action plus quick durations — **1 hour**, **2 hours**, **until
morning** — from the tray, the Ctrl+K command palette and the Shortcuts. VIP
contacts and keyword highlights still break through, exactly as during the
schedule, and unread badges keep updating; only the popup is held back. A timed
snooze clears itself when it runs out.

**Summarise your unread chats with the local AI (idea #5).** A new
**AI: Summarise unread chats** action (right-click menu, Ctrl+K command palette,
Shortcuts) reads your unread conversations — who, how many, and a preview of the
latest message — and asks the assistant for one short, prioritised digest of
what needs a reply, in the conversation's own language. It never opens a chat
(so nothing is marked read) and, like every AI action, the request is made from
C++ against your configured endpoint, so it can run entirely on a local model
(Ollama) and never touches WhatsApp Web. Joins the existing summarise-chat,
improve-message and suggest-reply actions.

**A corrupt profile no longer costs you the link to your phone.** When
QtWebEngine's IndexedDB goes corrupt, Chromium recovers by deleting the
database — which throws away WhatsApp Web's multi-device session keys, so the
next launch shows a QR code and you have to re-link the phone. Whatly now keeps
a small, recent snapshot of each account's session storage (only the parts that
carry the session, not the caches) and, when it starts up to find a wiped
session with a good snapshot behind it, restores it automatically — no re-link.
The snapshot is refreshed at startup while nothing has the database open, so it
is always consistent and never the cause of corruption. It can be turned off
with the `sessionBackup/enabled` setting.

**A corrupt Service Worker cache no longer leaves WhatsApp Web stuck.** When the
Service Worker's on-disk cache is corrupt it fails to register, and that helps
stall WhatsApp Web's bootstrap into the "unresolved dependencies" cascade (#43).
Chromium self-heals a corrupt IndexedDB but not this cache, so the failure used
to persist across reloads. Whatly now notices the registration failure and, once
per page, clears the page's caches and service workers with the browser's own
APIs and reloads, so the Service Worker installs clean. Nothing is deleted from
disk by the app, and only the current origin is touched.

**Quieter, more useful console log.** WhatsApp's de-identified-telemetry beacon
is blocked by CORS (its endpoint sends no `Access-Control-Allow-Origin` header,
the same in a plain browser) and its Permissions-Policy header names features
this Chromium build does not implement; both repeat by the hundred and neither
means anything is wrong. Reason-less promise rejections ("Uncaught (in promise)
undefined"), which WhatsApp Web floods when its session state is unhealthy, are
filtered too. All are now dropped, so the one line a bug report needs is no
longer buried under identical noise (a rejection that carries a real value, or
any other error, still shows).

## 7.1.0 (2026-08-11)

Whatly 7.1.0 builds on 7.0.0. Highlights: spell-check dictionaries are now
downloadable per language, so a fresh install ships only `en_US` (~45 MB
smaller) and fetches the rest on demand, each verified by SHA-256, including
Esperanto and other languages that were never bundled. The custom window frame
resizes from
every edge and corner, the unread badge is configurable and read from
WhatsApp's own store, the collapsed chat strip shows unread counts, no window
is treated as "the main one", and the running build identifies itself in the
window and the log. Plus a clearer notice when WhatsApp Web fails to load, Noto
Sans pulled in by the packages, a lighter Flatpak, and many account and window
fixes.

**Spell-check dictionaries can be downloaded per language (#46).** Every install
carried all ~45 MB of dictionaries even though almost nobody needs more than a
few. A new folding **Spell-check dictionaries** section in Settings now lists
each language with its size and a Download/Delete button, and the language picker
offers not-yet-installed languages too — ticking one downloads it. Each `.bdic`
is verified by SHA-256 before it is used, and on first run the dictionary for
your system language is fetched automatically. Fresh installs now bundle only
`en_US` and pull the rest on demand from a dedicated `dictionaries` release,
down from ~45 MB; languages that were never bundled — Esperanto among them — can
finally be installed from the UI. (Offline builds can still bundle everything
with `-DWHATLY_BUNDLE_DICTIONARIES=""`.)

**Spell-check dictionary cleanup.** The bundled set carried a duplicate `en-US` (identical to `en_US` bar the separator, and never selectable) and an invalid `vi_VI` region code; both are gone, keeping `en_US` and `vi_VN`. Groundwork for making dictionaries downloadable per language (#46).

**A clearer message when WhatsApp Web fails to load.** When WhatsApp Web's own
module loader collapses (its "unresolved dependencies / cr:NNNN is not defined"
cascade), the web app never finishes initialising and it looks as if login is
broken. Whatly now prints a single plain-language line saying it is not a login
problem (Whatly does not implement login) and how to recover (reload, or
clear the cache/data and relaunch) instead of leaving only WhatsApp's
cryptic error. It is captured in the log a bug report carries (#43).

**Noto Sans is now pulled in by the packages.** Qt logged
`qt.text.font.db: OpenType support missing for "Noto Sans", script …` when the
system had no Noto font with OpenType tables for a given script. Text still
rendered via fallback, but to fix it at the source the packages now bring Noto
Sans in: a Recommends on the `.deb`/RPM (Debian `fonts-noto-core`, Fedora
`google-noto-sans-fonts`, openSUSE `noto-sans-fonts`), a depends on the AUR
packages (`noto-fonts`), `media-fonts/noto` in the Gentoo ebuild, and it is
bundled into the snap. The Flatpak already ships Noto via its KDE runtime.

**The build identifies itself at startup.** The first line of output is now the
version, the commit, the branch and the build time. A bug report, or a tester
saying a fix did not work, is only worth as much as the certainty about what was
running: an install that silently did not replace the previous one, and a build
from the wrong branch, both look exactly like a fix that failed. It goes through
the normal log, so it is included in the output a bug report carries. Packagers
can set `-DWHATLY_BUILD_LABEL=" (…)"` to mark a one-off build; a normal build
prints nothing extra.

**Smaller Flatpak.** The Flatpak image no longer ships `webenginedriver`, the
WebDriver server the app never launches and no user can reach, trimming about
17 MB from every install (down to ~320 MB).

**Whatly reopens on the account you were last using.** With more than one
account, the app always started on the first tab, so anyone whose main number was
not first had to switch accounts by hand on every launch. The active account is
now remembered and restored. It is stored by account id rather than by position,
so reordering the tabs does not send the next start to the wrong account, and if
that account has since been removed the first tab is used as before.

**An account you are not using now costs nothing at all.** Every account built a
full WhatsApp Web page at startup and kept it for the whole session, so a
four-account setup downloaded the web app four times and paid for four renderer
processes before you had looked at any of them. Measured on a four-account setup
where three of the accounts were sitting on a QR screen doing nothing, those three
held about 720 MB — roughly a third of the entire application. With "suspend
inactive accounts" enabled, an account you have not opened has no page: not a
frozen one, not an empty one, none. It is built the moment something needs to draw
it — clicking its tab, switching to grid view, tearing it out into its own window
— and thrown away again once you have left it alone for the configured time, so it
goes back to costing nothing. Startup is also markedly quicker, because only the
account you land on is loaded. Previously this option only froze background
accounts, which stops their timers and scripts but keeps every byte of the
renderer, so the memory it was meant to save stayed allocated. The trade is
unchanged and still opt-in: an account with no page cannot notify you, so with
this off — the default — every account loads at startup exactly as before.
**Settings: the things in "Performance & Privacy" are now where you would look for
them.** That one section had grown into a grab-bag holding, besides the GPU and
memory options its name suggests, the entire AI assistant and inline translation
panels, whether photos are sent in HD, whether Enter holds a message briefly, the
chat-list preview blanking and the WebRTC privacy shield — so the thing you wanted
was almost never under the heading you would have guessed. **AI & translation** is
now a section of its own, the two privacy settings have joined **Privacy & Lock**,
the two messaging ones have joined **Chatting**, and what remains is genuinely
performance, so the section is simply called that. **Interface scale** has also
moved from "Network & Startup", where it sat beside the autostart checkbox, to
**Window & zoom** beside the other zoom controls. No setting changed its meaning and
nothing was renamed; they are only in sensible places now.
**The custom window frame can be resized from any edge or corner, and detached
windows get the same frame.** With the custom frame on, the only way to resize was
a single grip in the bottom-right corner — one of the eight places a normal window
can be grabbed — because dropping the native decoration drops the native resize
border with it. All four edges and all four corners now work, with the right cursor
on each, and the drag is handed to the window manager so it behaves exactly as a
normal window's does. Detached account windows now wear the custom frame too rather
than being the one window left with the system's, which is why they needed the
resize borders first. They also gain the trailing "+" tab the main strip has, and
an account added from a detached window's "+" now appears in that window instead of
jumping to the main one.

**The collapsed chat list shows unread counts again.** Collapsing the list to a
strip of avatars cut WhatsApp's own unread badge off the right-hand edge, so the one
thing the narrow list most needs to tell you — which conversations are waiting — was
only visible by hovering each row in turn. Each collapsed row now carries a small
green count in its top-right corner, and no badge at all when there is nothing
unread. The number is WhatsApp's own, read from the row rather than tracked
separately, so it cannot drift out of step, and it is drawn without adding anything
to the page.

## 7.0.0 (2026-08-03)

Whatly 7.0.0 is a major feature release. Highlights: a built-in AI assistant
(any OpenAI-compatible endpoint, with a one-click local-Ollama helper), inline
translation, chat and media export, undo-send, reply straight from
notifications, a quick-compose overlay, native Wayland in the portable builds,
and new openSUSE and Gentoo packages, plus two App Lock security fixes. The full
list follows.

**New packaging: openSUSE and Gentoo.** openSUSE Tumbleweed gets a native `.rpm`
(built against the distro's Qt 6.10, no bundling) attached to every release, plus
an Open Build Service recipe (`packaging/obs/`) to publish it from your own OBS
project. Gentoo gets an overlay with the `net-im/whatly` ebuild
(`packaging/gentoo/`), versioned and `-9999` live, which can enable the
proprietary video codecs via `dev-qt/qtwebengine[proprietary-codecs]`.

**Security: App Lock now blocks sending while locked (#41).** Sending a message
did not check the lock, so while Whatly was locked a message could still be sent
via Quick Compose (Ctrl+Alt+N), an inline notification reply, the CLI `--send` or
the local HTTP API. All of these now refuse while locked and tell you to unlock
first; Quick Compose no longer even opens. Scheduled messages have their own
delivery path and still fire while locked, as intended.

**Security: App Lock passcode is now hashed, not reversible (#42).** The App Lock
passcode was stored as Base64 of the plaintext (recoverable with `base64 -d` from
the config file) and was even shown in Settings. It is now stored as a salted
PBKDF2-SHA256 hash and verified by hashing, so the stored value cannot be turned
back into the passcode; the Settings view no longer displays it. Existing
passcodes keep working and are upgraded to the hashed form on the next unlock.

**Heads-up when the build lacks video codecs (#34).** The portable builds
(AppImage/.deb) use a Qt WebEngine without the proprietary H.264/AAC codecs, so
WhatsApp cannot process MP4 videos and rejects them as "not supported" (photos
and WebM/VP9 videos are fine). Whatly now detects this at runtime and shows a
one-time notice explaining it and the workaround, instead of leaving the failure
unexplained. Distro/native packages built with the codecs (system Qt) are
unaffected.

**Fixed: notifications showed a broken/unknown app logo on KDE (#38).** Desktop
notifications asked the notification daemon for an icon named "whatly", which is
not the installed icon name (net.shakaran.whatly), so KDE (and Flatpak in
particular, where only that icon exists) drew a generic "unknown app" logo. They
now use the correct icon name and set the desktop-entry hint, so the Whatly logo
appears and the notification is attributed to the app.

**Four more interface languages.** Persian/Farsi (fa), Ukrainian (uk),
Vietnamese (vi) and Traditional Chinese (zh_TW) join the interface translations,
bringing the total to 20. Pick one in Settings → Interface, or leave it on the
system default.

**Quick-compose overlay (#4).** A small always-on-top box, summoned by a global
hotkey (Ctrl+Alt+N) or from the command palette ("Quick message…"), lets you send
a message without opening the window: type a contact name or phone number, type
the message, press Enter. It sends through the running session (so the window can
stay hidden) and closes itself on send, Escape, or losing focus. The global
hotkey is registered via the same desktop-portal/X11 path as Ctrl+Alt+W; where
the portal cannot bind it, the command palette entry still works.

**Native Wayland in the portable builds (#8/#36).** The AppImage and .deb/.rpm
previously ran through Xwayland (stuttery scroll, blurrier fonts) because the Qt
used in CI ships no Wayland client platform plugin. The build now compiles that
plugin from the matching Qt source and bundles it, so the app runs natively on
Wayland where available. This is a packaging change only; it is best-effort in
CI (if the plugin cannot be built the AppImage still ships with the previous
Xwayland fallback rather than failing), so it wants a real artifact build to
confirm before relying on it.

**AI assistant.** A new Settings → AI assistant section connects Whatly to any
OpenAI-compatible `/chat/completions` endpoint (OpenAI, OpenRouter, Groq, or a
local runner like Ollama or LM Studio, which keeps everything on your machine).
Three actions: "AI: Summarise chat" shows a summary of the open conversation,
"AI: Improve message" rewrites your draft in the message box, and "AI: Suggest a
reply" proposes a response and puts it in the box to review before sending. The
chat text is sent to the endpoint you choose, so pick one you trust; the request
is made by the app, so the endpoint and optional API key never reach WhatsApp
Web. Off by default. A persistent toast shows while the model works, and a
warning is shown when free memory is low (a local model can need several GB).

**Right-click menu for text actions.** The AI, translation and export actions are
now on the message view's right-click menu (composer actions in the message box,
selection actions when text is selected), so they no longer need the command
palette or a shortcut. They remain in the command palette and Shortcuts too.

**Local AI made easy (Ollama helper).** Settings → AI assistant can now detect a
local Ollama, list its installed models to pick from a dropdown (no typing), and
download a recommended light model (qwen2.5:3b, llama3.2, gemma2:2b, phi3) with a
button and a progress bar. This makes a private, on-device assistant approachable
without touching a terminal. A light model is recommended: large models can
exhaust memory and slow the app.

**Reply from notifications.** On desktops whose notification service supports it
(KDE Plasma, and others that advertise the freedesktop `inline-reply`
capability), message notifications now carry a reply field: type an answer and it
is sent straight to that chat, no window needed. Whatly posts these through
`org.freedesktop.Notifications` itself, so the reply text stays local and the
per-contact icon is preserved. Where the service does not support it, or when the
option is turned off (Settings → Notifications), notifications behave exactly as
before.

**Export chat.** A new "Export chat" action (command palette and Shortcuts)
saves the open conversation to a folder you pick: a WhatsApp-style `chat.txt`
transcript, a structured `chat.json`, and a `media/` folder with the images,
videos and audio that were loaded. It scrolls the whole conversation to pull in
history first (WhatsApp Web only keeps a small window of messages in memory at a
time), and downloads each attachment while it is on screen. Media that never
finished loading is noted in the transcript rather than saved. All local; no
data leaves the machine.

**Inline translation.** A new Settings → Performance section connects Whatly to
a LibreTranslate-compatible endpoint (self-hosted or otherwise). Two actions,
"Translate selection" and "Translate message box", are in the command palette
and can be bound to keys in Shortcuts: the first shows the translation of the
selected text in a toast, the second translates what you have typed and puts it
back in the message box before you send. The target language follows the app's
language by default (configurable); the source is detected automatically. The
request is made by the app itself, so the endpoint and optional API key never
reach WhatsApp Web. Off by default.

**Undo send.** A new Settings → Performance option holds a message for a few
seconds after you press Enter, showing an "Undo" button before it is actually
sent, so a mistaken Enter no longer sends instantly. Pressing Enter again sends
at once, and clicking Undo keeps the text in the composer to edit. The delay is
configurable (default 5 s) and the whole feature is off by default.

**A progress bar for dropped attachments.** Dropping a file read it, base64-encoded
it and built the injected script all in the drop handler, so the window froze for
several seconds (on a video, long enough to look like a hang), with nothing on
screen to explain it. The reading now happens on a worker thread with a small
progress bar over the bottom of the chat, so the window stays responsive and the
wait is visible. The bar waits a moment before appearing, so a quick drop does
not flash one up and away. Files that do not fit the 64 MB drop buffer were
skipped with only a terminal warning; they are now named on screen as well. A
file dropped while another is still being read is queued and attached after it,
rather than being ignored.

**VIP and muted contacts for notifications.** Settings → Notifications now takes
a list of VIP contacts, which always notify even during Do Not Disturb, and a
list of muted contacts, whose popups are never shown (their unread badge still
updates). Names are matched case-insensitively against the sender.

**Recent unread chats in the tray menu.** The tray menu now has a "Recent
unread" submenu listing conversations with unread messages (name and count);
picking one brings the window up and opens that chat. It hides itself when
nothing is unread.

**Suspend inactive accounts to save memory.** With more than one account, each is
a full page holding hundreds of MB. A new Settings → Performance option freezes
the pages of accounts you are not viewing after an idle timeout, giving that
memory back, and wakes them when you switch. Off by default; a suspended account
does not receive messages until you switch back to it, and single-account setups
are never affected.

**Flatpak drag-and-drop from media folders (#32).** Files dragged from a host
file manager arrive as a plain `file://` path with no portal token, which the
sandbox could not read outside `~/Downloads`. The Flatpak now also gets
read-only access to the standard Pictures, Videos, Documents and Music folders,
so dragging media and documents from those works. It stays scoped to those
folders (no whole-home access); files elsewhere still need a portal-aware source.

## 6.8.4 (2026-07-31)

**Font hinting option (#37).** WhatsApp Web glyphs could render with heavier
hinting than a stock browser, since Qt WebEngine follows the system fontconfig.
Settings → Performance now has a "Font hinting" control (Automatic, None,
Slight, Medium, Full) that maps to Chromium's `--font-render-hinting`, so a
lighter level can be chosen when text looks heavy or uneven. Automatic (the
default) keeps the current behaviour. Applied at start-up, so it needs a restart.

## 6.8.3 (2026-07-31)

**Drag and drop no longer needs the FileTransfer portal outside a sandbox (#34).**
On a normal install the dropped files are read directly from their real paths, so
the app no longer tries (and noisily fails) the XDG FileTransfer portal on
desktops whose portal does not expose that interface. The portal is still used as
the fallback inside the Flatpak sandbox, where the real paths are not readable.

**No more "HD quality" dialog loop when attaching (#34).** With "Send photos and
videos in HD by default" on, attaching an image below HD resolution made WhatsApp
answer with a "this media is not HD resolution" dialog, and Whatly kept
re-clicking HD, which re-opened the dialog on every press until the app was
restarted. Whatly now enables HD at most once per media editor, so the dialog no
longer loops.

**Chat themes recolour the message-options button too (#35).** On a chat theme,
the little fade behind the "options" button on your own text messages stayed
WhatsApp's default green over a themed (for example pink) bubble. The theme now
also recolours WhatsApp's `--*-RGB` design-system colour tokens (stored as bare
`r, g, b` channels), which that fade uses, so it matches the bubble.

## 6.8.2 (2026-07-30)

**Add spell-check dictionaries without repacking (#24).** Whatly now keeps a
writable `qtwebengine_dictionaries` folder under your data directory and mirrors
the bundled dictionaries into it on start-up, so you can drop an extra
`<language>.bdic` (from a `hunspell-<language>` package) beside them and it is
picked up automatically. It merges with the bundled set rather than replacing
it, and works even for the AppImage, Flatpak and snap, whose own bundle is
read-only. Setting `QTWEBENGINE_DICTIONARIES_PATH` still overrides everything.

## 6.8.1 (2026-07-30)

**Group invite links now open (#186).** Clicking a `chat.whatsapp.com` group
invite (which reaches the app as a `whatsapp://chat?code=…` link) used to do
nothing. Whatly now opens WhatsApp Web's own "Join group" preview for it, so the
invite can be accepted from the desktop app.

**A sound for new-message notifications (#120).** Notifications now ask the
desktop's notification service to play its new-message sound, which some setups
stayed silent without. It is on by default and can be turned off under
Notifications with the new "Play sound" option.

**Voice and video calls now open in their own Whatly window.** WhatsApp Web's
"Move to new window" during a call (the call popout) used to do nothing: the
window request was handed to the browser or discarded, so the popped-out call had
nowhere to live. Whatly now hosts that popout in a proper Whatly window, with the
microphone, camera and screen-share permissions it needs, and closes it when the
call ends. Genuine external links still open in your browser as before. Voice and
video calls themselves work out of the box (#106, #43, #287, #112, #218, #187).

**Each account tab shows its WhatsApp Web version.** Hovering an account tab now
lists the WhatsApp Web version in use and the page build token, so a mismatch
between accounts is visible at a glance. The tooltip is translated into all
sixteen languages.

## 6.8.0 (2026-07-28)

**No more endless "Render process exited" dialog loop (#28).** When WhatsApp
Web's renderer kept terminating (seen in the Flatpak, repeatedly SIGTERM'd with
code 15), every termination popped its own modal, stacking without end, and with
auto-reload on it just relaunched a renderer that died again. Whatly now counts
terminations within a short window and, after a few in a row, stops reloading and
shows a single notice pointing at Settings → Performance (GPU) instead of looping.
Thanks @KAMiKAZOW for the report.

**Collapse the chat list to a strip of profile pictures.** A button in
WhatsApp's own sidebar (and a command-palette entry) folds the chat list down to
a column of avatars, giving that width back to the conversation; hovering an
avatar shows the name and last message, and the state is remembered. Chats only,
withdrawn in Calls/Status/Channels. Thanks @gbmaizol.

**A "Restart now" button for settings that need a relaunch.** It comes back as
the same program with the same command line, restoring the windows and the
settings page exactly as they were. The Cloud/Local API groups gained collapsible
headers, an open section now has an outline, and picking a tray-menu entry brings
the window to the front. Thanks @gbmaizol.

**Optional account strip and a hide-the-title-bar option.** The account tab strip
can now be hidden when there is only one account (off by default), and the custom
window frame can merge the window buttons into the account strip to drop the
title row entirely. Both under Window & zoom. Thanks @gbmaizol.

**Esperanto interface.** A sixteenth language, contributed and dogfooded by
@gbmaizol.

**Drag and drop works in the Flatpak now (#32).** A file dragged from outside
`~/Downloads` silently did nothing under the Flatpak sandbox, because the drop's
host path was not visible inside it. The drop now resolves files through the XDG
FileTransfer portal (sandbox-readable paths), falling back to the plain local
path on native and Windows builds, and warns loudly when a drop yields nothing.
Thanks @gbmaizol for the report.

**Install from the AUR (#30).** Arch packaging landed: `whatly` (builds from the
release source with Qt 6) and `whatly-bin` (the prebuilt AppImage). CI
build-tests the source package and publishes both to the AUR on each release.
Thanks @mysteryx93.

## 6.7.2 (2026-07-28)

**Drag and drop files to send them (#285).** Dropping one or more files onto the
window now hands them to the open chat as attachments, opening WhatsApp Web's
media editor (images and videos) or document preview (everything else) ready to
send. Qt WebEngine does not forward an OS file drop to the page, so the drop is
caught, the files are read (up to 64 MB total), rebuilt as a `DataTransfer`, and
handed to the composer with a synthetic paste. Works for any file type, not just
images. Covered by unit tests (`TstDropAttach`).

## 6.7.1 (2026-07-28)

**Cleaner monochrome tray icon (#14).** The faint dark outline under the light
glyph is now much subtler, so it is not noticeable on the usual dark panel while
still keeping the icon legible on a light one. Thanks to @Sadi58.

**Windows .msi installer.** Releases now ship a proper `.msi` installer for
Windows alongside the portable `.zip`. It installs to Program Files, adds Start
Menu and Desktop shortcuts, appears in Add/Remove Programs, and upgrades an
earlier install in place. Built in CI with the WiX Toolset from the same
`windeployqt` output as the zip.

## 6.7.0 (2026-07-27)

**Windows build & startup fixes.** Configuring with spell-check on now finds
`qwebengine_convert_dict` in Qt's `bin/` (where Windows and macOS keep it, vs
`libexec/` on Linux), instead of failing the build (#18, thanks @gbmaizol). And
launching `whatly.exe` from the Windows system directory — what a bare shell
prompt hands you — no longer aborts at start-up: Qt WebEngine was resolving
Chromium's DLLs against the wrong copies, so we now step out of `System32` while
leaving every other working directory (and relative command-line paths)
untouched (#19, thanks @gbmaizol).

**Tests no longer touch your real settings (#23).** The machine-wide
`Performance`/`NetworkProxy` store ignores the application name (it is read
before `QApplication` exists), so running the suite used to rewrite the
developer's own config — leaving the interface scale at 300% and a broken proxy.
The suite now redirects that store via `WHATLY_SETTINGS_APP`. Thanks @gbmaizol.

**Lower baseline memory (#15).** The Chromium renderer now starts V8 in
*optimize-for-size* mode by default, trimming the idle `QtWebEngineProcess`
footprint at a negligible speed cost — sensible for an app that lives in the
tray. It shares the single `--js-flags` slot with the existing **JS memory
limit** setting (Settings → Performance): set an explicit cap and that stronger,
user-chosen bound takes over. Can be turned off via `perf/optimizeForSize`.
Covered by unit tests (`TstPerformance::optimizeForSizeFlag`,
`optimizeForSizeDefaultsOn`).

**Monochrome tray icon now works with no unread messages (#14).** The
monochrome choice appeared to do nothing (KDE Plasma, and likely everywhere)
because the idle tray icon — the one shown whenever the inbox is clear — bypassed
the icon composition and always drew the fixed colour icon; only the unread-count
icon honoured the setting. The idle icon now goes through the same path, so it
respects both the monochrome choice and the connection state. The composition is
also hardened: if the symbolic SVG can't be rendered on some setup it falls back
to the colour icon's shape, and if even that fails it degrades to the full-colour
icon instead of leaving an empty tray slot. Covered by unit tests (`TstTrayIcon`,
including the idle-honours-monochrome regression). Thanks to @Sadi58.

**Adding an account is discoverable now.** The account strip (with its trailing
"+") is shown in tabbed view even with a single account, and the tray menu gains
an **Add account…** entry — so a second account no longer requires knowing the
`Ctrl+K` command palette. Same on every platform (the tabs were never
Windows-gated; they were just hidden until a second account existed).

**Never strand the window when the tray icon is hidden (#13).** With **Hide tray
icon** on, minimising (Ctrl+W, or *minimise in tray on start*) used to hide the
window with nothing left to click to bring it back — the app kept running,
invisible. Minimise now hides to the tray only while a tray icon is actually
there, and otherwise minimises to the taskbar; and **Hide tray icon** is now
mutually exclusive with *start minimised* and *minimise on tray-icon click*, so
the broken combination can't be built in the first place. Thanks to @gbmaizol.
Covered by a unit test (`TstSettings::traySettingsMutuallyExclusive`).

## 6.6.0 (2026-07-26)

**Start-up crash on newer distros fixed (#11, #12).** The Linux packages
(AppImage, .deb, .rpm) bundle the linked NSS libraries but were missing the
PKCS#11 modules NSS loads at runtime (`libsoftokn3`, `libfreebl3`,
`libnssckbi`). On distros with a newer NSS (e.g. Debian 14, PikaOS) the app
loaded the host's `libsoftokn3` against the bundled older `libnssutil3`, which
lacks the `NSSUTIL_3.x` symbol version it needs, and Chromium aborted on
launch (`nss_util.cc … FATAL`). The packaging now ships the matching NSS module
set, and CI fails the build if they go missing again.

**Zoom buttons in WhatsApp's sidebar.** Zoom out / reset / zoom in buttons can
now be added to WhatsApp Web's own left rail (next to the theme and blur
buttons), so you can scale the page live without opening Settings or reaching
for Ctrl +/−/0. They drive the same page zoom, clamped to a sane range (the
clamp is unit-tested, `TstZoom`). Toggle them in **Settings → Appearance →
"Zoom buttons in WhatsApp's sidebar"**. Note: this is the WhatsApp *page* zoom
(instant); the separate interface-scale setting scales the whole Qt UI and still
needs a restart.

**Send messages from the command line (work in progress).** `whatly --send
--to <number> --message "…"` hands a message to the already-running instance of
that profile and it goes out through the WhatsApp Web session — scriptable, per
profile. `--file <path>` attaches an image or file with `--message`/`--caption` as its
caption (the caption is attached to the media in a single message), up to 3 MB
over the web backend. Reusable **message
templates** (per account) let you save a body with `{{fields}}` and send it
with `--send --template <name> --var key=value`; manage them with
`--template-set name=body`, `--template-list` and `--template-remove`.
`--backend web` (default) uses the live session; `--backend cloud` (Meta
WhatsApp Business Cloud API), groups/contact-name recipients and a local API are
being added on top. Covered by unit tests (`TstMessaging`,
`TstMessageTemplates`). Note: automating WhatsApp Web is, strictly, against
WhatsApp's terms — use it for your own account.

**Auto-reply to incoming messages (work in progress).** An opt-in "listener"
replies automatically to messages in the open conversation using rules —
`exact`, `contains`, `hashtag` or `regex` (with `$1..$9` capture substitution in
the reply). Rules live per account and/or in a JSON file you maintain; manage
from the CLI: `--autoreply-on` / `--autoreply-off`, `--autoreply-file <path>`,
`--autoreply-list`. The rules engine and store are covered by unit tests
(`TstAutoReply`); the page-side observer detects incoming messages by bubble
position (current WhatsApp Web obfuscates its class names) and was verified live
end-to-end, including regex captures in the reply. It replies in the open
conversation. Same terms-of-service caveat as above.

**Send to a contact or group by name.** `--send` over the web backend now
accepts a non-number recipient: `--to "Alice Smith"` (or `--to name:Alice`)
opens the chat whose title matches **exactly** (case-insensitive) via WhatsApp
Web's search and sends — it aborts rather than message the wrong chat if there
is no exact match. A group given by name works the same way. A group id
(`--to group:<id>` or `<id>@g.us`) is resolved to its chat title through
WhatsApp Web's internal module loader and then opened by that title. An
attachment works too — `--file <path>` with `--message`/`--caption` opens the
matched chat and sends the media with its caption (up to 3 MB, same as the
phone-number path). Verified live end-to-end (text, attachment, group by name
and group by id). This is page automation, so it can still break when WhatsApp
Web changes its markup.

**Cloud API backend for sending (work in progress).** `--send --backend cloud`
now sends directly through the Meta WhatsApp Business Cloud API — no running
WhatsApp Web session needed, so it works headless and scriptable. It supports
plain text, media (`--file`, uploaded then sent with an optional caption) and
Meta-approved **templates** (`--cloud-template <name> --cloud-lang <code>
--cloud-param <value>…` for positional `{{1}},{{2}}…` body parameters).
Configure it per account with `--cloud-phone-id`, `--cloud-token` and
`--cloud-api-version` (the access token is one you supply and is stored in the
account config; Whatly never obtains it itself), and check it with
`--cloud-status`. The same phone-number id, access token and Graph API version
can now also be set in **Settings → Cloud API** instead of on the command line.
The URL/payload builders are covered by unit tests (`TstCloudApi`).

**Cloud API webhooks — auto-reply without a browser (work in progress).** The
Cloud backend can now *receive* messages too: Whatly serves Meta's webhook on
the local API port (`GET /webhook` verify handshake + `POST /webhook` events),
validates the `X-Hub-Signature-256` HMAC against your Meta app secret, and runs
each incoming message through the same auto-reply rules — replying through the
Cloud API, no WhatsApp Web session involved. Because it rides the loopback
local-API port, expose it to Meta with a tunnel or reverse proxy
(cloudflared/ngrok → `127.0.0.1`) rather than opening Whatly to the network.
Configure with `--webhook-on` / `--webhook-off`, `--webhook-verify-token`,
`--webhook-app-secret` and `--webhook-status`. Verification, signature and
payload parsing are covered by unit tests (`TstCloudWebhook`).

**Local HTTP API for sending (work in progress).** An opt-in HTTP endpoint lets
other programs on the same machine send through the running instance — the
scriptable counterpart of `whatly --send`, callable over HTTP (cron jobs,
home-server automations, other languages). `POST /send` with a JSON body
(`{"to":"…","message":"…","file":"…","backend":"web|cloud"}`) queues the send
and returns `202`. It binds to the loopback interface only (`127.0.0.1`) and
every request must carry a bearer token, so it is never reachable from the
network; it is off until you enable it. Configure with `--localapi-on` /
`--localapi-off`, `--localapi-port` (default 8590), `--localapi-token`, and
`--localapi-status`, or in **Settings → Local API & Cloud webhooks** (which also
holds the webhook fields below). Request parsing, auth and the JSON→command
mapping are covered by unit tests (`TstLocalApi`).

**Multi-window account tabs (#10).** Multiple accounts now live in a
Chrome-style tab strip: add one with **+**, rename or close from a right-click
menu, and drag a tab out of the strip to **tear it off into its own window**
(or drop it back to redock). A **grid view** tiles every account side by side
with resizable panes and thin scrollbars, and the whole multi-window
arrangement (which account is where, each window's geometry and active tab, tile
sizes) is optionally restored on the next launch. Each account stays its own
Chromium storage partition, so sessions never touch and the tray badge still
sums unread across them all. Thanks to @gbmaizol.

## 6.5.0 (2026-07-24)

**Recover from a start-up crash (#3).** If Qt WebEngine aborts while
initialising GPU/GL against an incompatible system driver — seen on some
Linux setups before WhatsApp Web ever loads — the next launch automatically
switches to progressively safer software rendering and tells you so; a clean
load resets it. Chromium's own diagnostics are now also written to a log file
(`whatly-webengine.log`) on a desktop/systemd launch, so the crash cause lands
in a bug report instead of vanishing into the journal. The release packaging
now verifies the bundled Qt WebEngine runtime is complete before shipping.
Covered by unit tests (`TstPerformance`, `TstDebugLog`).

**Settings, reorganised (#9).** The long "General settings" list is split into
themed, collapsible sections — Basics, Appearance, Notifications, Chatting,
Privacy & Lock, Window & zoom, Advanced — each with an arrow header; only the
first is open on launch. Language names read in their own language without the
redundant territory ("Español", not "Español de España"). Fully translated into
all 15 languages. Covered by unit tests (`tst_settings`).

**Emoji skin-tone selection (#6).** Picking a skin tone no longer closes the
emoji panel mid-selection — the variant/skin-tone popover is now recognised as
part of the emoji subsystem.

**HD photos on receive (#7).** WhatsApp Web's `wa_web_show_hd_photo` flag is
forced on, so HD photos render the moment the servers deliver them to linked
devices. This is the receive side; sending in HD is a separate option.

**Notification popup stays on-screen (#5).** The custom notification popup is
anchored to the top-right of the active screen's available area with a margin,
so its edges are no longer clipped (including on a secondary monitor).

**Consistent download path (#4).** The default download location uses forward
slashes on every platform, instead of a mixed separator on Windows.

**Hide from the tray on Windows (#8).** Clicking the tray icon while the window
is frontmost hides it again as intended — a short grace window recovers the
focus the shell steals on the click. Windows-only; other platforms unchanged.

**Raise the window from the tray.** A left click (or double click) on the
tray icon now reliably brings the window to the front — shown, un-minimised
and focused — which used to fail on Windows and when minimised. Only a
click while the window is already frontmost hides it, and only if you
enabled *minimize on tray-icon click*.

**Send media in HD by default.** An optional tweak (Settings → Performance
& Privacy) selects HD in WhatsApp Web's media editor automatically, so
photos and videos default to HD. It depends on WhatsApp Web's layout and
can be turned off if an update breaks it. Covered by unit tests
(`TstHdMedia`).

**Acknowledgements.** The linked-device identification, the dark-theme
persistence fix, and Windows support build on pull requests by Gert Bolten
Maizonave (gbmaizol) to upstream WhatSie (#324, #326, #321).

## 6.4.0 (2026-07-20)

A large feature release: a command palette, notification rules, recurring
schedules and reminders, an update checker, storage and shortcut management,
profile backup, screen-lock integration, working screen sharing, focus mode and
saved replies.

**Accessibility.** The icon-only controls added in this release (the custom
title bar's window buttons, the command palette's search field and result
list) now carry accessible names, and each grid tile's view is named after
its account — so a screen reader announces something meaningful instead of
nothing.

**Saved replies.** Store the short texts you send often (Settings → *Saved
replies*) and insert one from the command palette: `Ctrl+K`, type
"Insert", pick it, and the text is typed into the message box — going
through the page's own input handling, so the Send button enables and
drafts are tracked as if you had typed it. Covered by unit tests
(`TstCannedResponses`).

**Grid view captions.** Each tile in the multi-account grid now carries a
caption with the account's name and its unread count, so it is obvious
which tile is which; the caption tracks renames and unread changes.

**Focus mode.** A new privacy toggle masks the contact names and message
previews in the chat list (hover to reveal one), leaving the conversation
you are actually reading untouched — for screen sharing, screenshots and
open-plan desks, where the hover-to-reveal privacy blur is still too
revealing. Covered by unit tests (`TstFocusMode`).

**Screen sharing in calls.** Screen-share requests from WhatsApp Web were
being dropped silently (a black screen for the other side). Whatly now
shows a picker listing your screens and windows, and routes WebRTC capture
through PipeWire and the desktop portal so it works on Wayland as well as
X11 (a new *Performance & Privacy* toggle, on by default on Linux). The
camera/microphone/screen permission prompts were already handled.

**Quick reply from a notification.** Clicking a notification now opens the
chat *and* puts the caret in the message box, so you can reply by just
typing — no extra click. (A text field inside the notification itself is
deliberately not used: the XDG portal spec has no standard entry field and
the freedesktop inline-reply capability is not available on every backend.)

**Lock when the screen locks.** With a passcode configured, Whatly can now
lock itself the moment the desktop session locks (it listens for the
freedesktop/GNOME screensaver signal over D-Bus), not just on a timer or
when hiding to the tray. Opt in from the app-lock settings. Covered by
unit tests (`TstScreenLock`).

**Profile backup & restore.** Settings → Storage can now export an entire
account — settings, the logged-in session and your custom CSS/JS addons —
to a single `.tar.gz`, and import one back (with a clear warning that the
archive holds your session and that importing overwrites the current
account). The archive/extract/copy primitives are covered by unit tests
(`TstBackup`).

**Customisable keyboard shortcuts.** Settings → *Keyboard shortcuts* lets
you remap the app's actions (reload, lock, mute, theme, grid, command
palette, quit, …). Clashing a shortcut with another action is rejected;
clearing a field removes it. Changes apply after a restart. The registry,
override round-trip and conflict detection are covered by unit tests
(`TstShortcuts`).

**Storage manager.** Settings → Storage now also shows the cache size and
has its own *Clear cache* button (separate from clearing all persistent
data), so you can reclaim space without signing out. The size helpers are
covered by unit tests (`TstStorageInfo`).

**Update checker.** Whatly can now check GitHub once a day for a newer
release and let you know with a click-through notification — it never
downloads or installs anything itself. Opt out in Settings → *Network &
Startup*. The version comparison and release parsing are covered by unit
tests (`TstUpdateCheck`).

**Recurring scheduled messages & reminders.** A scheduled message can now
repeat — daily, on weekdays, or weekly — rescheduling itself to the next
occurrence after each successful send. There is also a *remind me* mode
that pops a desktop notification at the due time instead of sending a
message. Covered by new unit tests for the next-occurrence maths and the
reschedule-on-send flow.

**Command palette (Ctrl+K).** A quick "do anything" switcher: press
`Ctrl+K`, type, and fuzzy-jump to any menu action, account, or the theme
toggle, then run it with Enter. The fuzzy matcher is covered by unit tests
(`TstFuzzy`).

**Do Not Disturb & keyword highlights.** Settings → notifications now has a daily
quiet period that suppresses notification popups (unread badges still update),
plus a list of highlight keywords that always break through — even during Do Not
Disturb — when they appear in a message. The DND window may wrap past midnight.
Covered by new unit tests (`TstNotificationRules`).

## 6.3.0 (2026-07-20)

A sweep of engine-tuning, connectivity and customisation features, with every
existing feature kept intact.

**Performance & privacy settings.** Settings → *Performance & Privacy* now
exposes the rendering-engine knobs that used to be hard-coded. Whatly still
disables the GPU by default on Linux (the long-standing fix for blank windows
and start-up crashes on some GPU/driver setups, issues #200 / #234 / #252), but
you can now turn acceleration back on, ignore the driver blocklist, run the GPU
in-process, toggle GPU compositing and VSync, or pick a lower-memory process
model (single-process / process-per-site). A JavaScript memory cap
(`--max-old-space-size`, for the "eats RAM" reports #241 / #255) and an HTTP
cache type/size control round it out. A *Prevent WebRTC IP leak* switch stops
WebRTC from revealing your local IP over non-proxied connections. All of these
are stored machine-wide and applied at start-up, so a change takes effect after
a restart. Covered by new unit tests (`TstPerformance`) and translated into all
15 languages.

**Network proxy.** Settings → *Network & Startup* now lets you route Whatly
through a proxy: *System* (follow the OS, the default), *None* (connect
directly), or a manual *SOCKS5* / *HTTP* proxy with host, port and optional
username/password. It is applied application-wide, so every account uses it, and
manual changes take effect for new connections without a restart. Covered by new
unit tests (`TstNetworkProxy`).

**Start at login.** The same section has a *Start Whatly when I log in* switch.
On Linux it manages an XDG autostart entry
(`~/.config/autostart/net.shakaran.whatly.desktop`); on Windows a per-user
`Run` entry. Covered by new unit tests (`TstAutostart`).

**Interface scale control.** You can now set an interface/content scale factor
from Settings instead of only via the `QT_SCALE_FACTOR` environment variable
(which still wins if set). It scales the whole window and the page together
(matching #203) and applies after a restart.

**Portal notifications (Flatpak).** Native notifications can now be delivered
through the XDG desktop portal (`org.freedesktop.portal.Notification`) instead of
libnotify. A Flatpak build cannot always reach the system notification service
directly, but it can always reach the portal. Settings → notifications has a new
*Notification delivery* choice on Linux: *Automatic* (use the portal inside a
Flatpak sandbox, the system service otherwise), *Desktop portal (Flatpak)*, or
*System service (libnotify)*. Clicking a portal notification still raises the
window and marks the chat. Covered by new unit tests (`TstPortalNotification`).

**Custom JavaScript addons.** Alongside the existing custom-CSS support, you can
now load your own `.js` files to run on WhatsApp Web (Settings → *Custom
JavaScript addons*). Add several, tick/untick each to enable or disable it, or
remove it. Every addon runs inside its own `try`/`catch` sandbox, so a broken
one can never take down the page or the other addons. Covered by new unit tests
(`TstCustomJs`).

**Per-account custom CSS/JS.** Custom CSS and the new JS addons are now stored
per account: the default account keeps its existing `custom.css` (nothing moves
on upgrade), while a named `--profile` account gets its own stylesheet and its
own addon set.

**Grid view for multiple accounts.** When you have several in-window accounts you
can now show them all at once in a tiled grid instead of one at a time. Toggle it
from the tray menu (*Grid view* / *Tabbed view*) or with `Ctrl+G`; the choice is
remembered. The existing layouts are untouched: the tabbed view remains the
default, and separate accounts in separate windows are still available by
launching with `--profile`.

**First-run setup wizard.** A new account is greeted by a short wizard that
offers a few sensible starting choices — match the system light/dark theme,
start at login, and (on Linux) how notifications are delivered — then points at
the QR code to sign in. Everything it sets is also in Settings, so it is purely a
friendlier on-ramp; it shows once and never again. Covered by new unit tests
(`TstSetupWizard`).

**Optional custom window frame.** For a more app-like look you can now replace
the system title bar with Whatly's own slim one (Settings → *Network & Startup* →
*Use a custom window frame*). It carries the minimise / maximise / close buttons,
drags via the compositor (so it works on Wayland and X11), double-click to
maximise, and a corner grip to resize. Off by default — the native decoration is
untouched unless you opt in — and it applies after a restart.

## 6.2.1 (2026-07-19)

Bug-fix and hardening release.

**Fix a crash in the automatic-theme setup.** Opening Settings → automatic theme
on a system with no geolocation backend (a headless run, or any box without a Qt
geo plugin) crashed on close: the dialog's destructor dereferenced a null
position source. Found by a new in-process SettingsWidget test.

**Clean shutdown on SIGTERM.** Whatly now quits gracefully when it receives
`SIGTERM` (from a session manager, `kill`, or systemd) instead of being torn
down abruptly, using the Qt-safe socketpair + `QSocketNotifier` pattern.

**Quieter terminal.** The benign "QThreadStorage: entry … destroyed before end
of thread" lines that Qt WebEngine prints while tearing down at exit are no
longer echoed to the terminal (they are still kept in the in-app debug log for
bug reports).

**About screen icons.** The buttons on the About dialog (Donate, Ko-fi, Wise,
Rate, More apps, Source code, Report a bug, Debug info) were rendering without
their icons. They are set from the bundled resources again, and two new icons —
`heart-line` and `github-line` — were added for the Ko-fi and Source-code
buttons. The Rate-the-app screen's logo and button icons were verified to be
correct and unchanged.

**Unit tests.** A QtTest suite now covers the headless parts of the app — the
`Utils` helpers (including the cache-delete guard from issue #230), the
injected-script generators (fonts, chat themes, muted status, privacy blur,
wallpaper, custom CSS, tweaks, linked-device name), the scheduled-message queue
and its persistence, the sun calculations, identicons, palettes, dictionary
resolution and the About/Rate screens' assets. Roughly 92% line / 95% function
coverage of that layer (measure it with `tools/coverage.sh`). Build with
`-DWHATLY_TESTS=ON`, run with `ctest`; it also runs in CI on every push.

## 6.2.0 (2026-07-18)

Desktop-integration features and finer control, from a sweep of the upstream
issue tracker.

**Taskbar unread badge.** The unread total is now published as a launcher badge
over the standard `com.canonical.Unity.LauncherEntry` D-Bus protocol, so KDE
Plasma's task manager, GNOME's Dash-to-Dock and others paint the count on
Whatly's taskbar button — no extra dependency, ignored where it isn't supported
(issue #122).

**Interface font size.** Settings → Appearance has a new *Interface font size*
control that scales the app's own chrome — menus, dialogs and Settings itself —
independently of WhatsApp Web's zoom. Applied live and on the next launch (issue
#76).

**Reload automatically after a crash.** An optional setting reloads WhatsApp
Web's page process by itself if it ever crashes, instead of interrupting you with
a prompt (issue #225).

**Network status in the tray.** The tray tooltip now reads "Waiting for network…"
while disconnected, so a silent drop after a suspend/resume is noticeable beyond
the dimmed icon (issue #208).

**HiDPI / 4K scaling.** Setting `QT_SCALE_FACTOR` now also scales WhatsApp Web's
content to match, so a single variable enlarges both the window chrome and the
page on high-density displays (issue #203). Setting `WHATLY_MAX_FPS` to a
non-zero value lifts Chromium's frame-rate cap for those who want it (issue #221).

**Build.** Source distributions can now build a subset of the spell-check
dictionaries with `-DWHATLY_DICTIONARIES="en-US;es-ES;…"` instead of all 31
(issue #61).

**Windows.** The release workflow can optionally code-sign the Windows build via
the SignPath Foundation free open-source programme; it stays off until configured
(issue #325, see `packaging/windows-signing.md`).

**macOS (experimental).** Whatly now builds as a macOS `.app` bundle; CI produces
a `.dmg` and attaches it to the release. It is **unsigned** (first launch needs a
right-click → Open) and has not yet been validated at runtime — community testing
is welcome (issue #119).

**Small screens.** The window and dialog minimum sizes now adapt to the display,
so Whatly fits on small screens such as a Linux phone in portrait instead of
being pinned too large (issue #239).

All new interface strings are translated into the 15 shipped languages.

## 6.1.1 (2026-07-18)

Bug-fix release.

**Sharper notification icons.** Native notifications carried their app icon as a
32-pixel image, which looked blurry on desktops that draw large notification
popups (for example Cinnamon on Linux Mint). Whatly now hands the notification
daemon a 256-pixel icon, so the logo stays crisp at any popup size (issue #2).

## 6.1.0 (2026-07-17)

New features and more ways to install.

**Scheduled messages.** Write a message to any number and have Whatly send it at
a time you pick. The schedule is saved to disk, so a message still goes out even
if Whatly was closed when it came due — the next time you open the app, anything
overdue is sent as a catch-up. While the app is open a timer sends due messages
on time. Manage the queue from **Scheduled messages…** in the tray menu. Sending
opens the recipient's chat (issue #250).

**Font family.** Settings → Appearance now has a *Font family* picker that
renders WhatsApp Web's text in any font installed on your system. Emoji, icons
and monospaced message formatting are left untouched (issue #219).

**Hide muted status updates.** A new toggle hides the "Muted updates" section of
the Status panel, so statuses from contacts you have muted do not show up at all
(issue #242).

**Donations.** Wise is now offered alongside Ko-fi and PayPal, in the About
dialog and the README.

**Packaging.** Whatly can now be built and installed as a **Flatpak** and an
**AppImage** (with `.zsync` delta updates), in addition to the snap, deb and
Fedora/COPR configurations. Flatpak and AppImage are built automatically on each
release and attached to the GitHub release. See `packaging/README.md`.

All new interface strings are translated into the 15 shipped languages.

## 6.0.0 (2026-07-16)

First release of the fork maintained at https://github.com/shakaran/whatly.
Everything below is on top of upstream 5.1.0.

**The app is now called Whatly** (it was WhatSie). The application id is
`net.shakaran.whatly`, the binary is `whatly`, and user data lives under
`shakaran/whatly`. Existing installs are carried over automatically on first
run — settings and the logged-in session are copied from the old `shakaran`
namespace and from the previous `WhatSie` layout, so nobody is logged out by
upgrading. If the automatic copy ever misses something, `whatly
--migrate-from=whatsie` does it by hand (`--dry-run` to preview first).

#### 🎁 Features

* **Multiple WhatsApp accounts.** Either as separate windows —
  `whatly --profile=<name>`, each with its own session, settings file and
  instance — or as tabs inside one window, added with a **+** and renamed or
  removed from a right-click menu. Every account is a separate Chromium storage
  partition, so the sessions never touch; the tray badge sums the unread count
  across them all. The default account keeps the exact storage it had, so an
  upgrade neither moves it nor logs it out, and the tab bar hides itself when
  there is only one account.
* **Spell checker, working.** It was not broken, it was gone — and the build
  asserted that the system hunspell package supplied the dictionaries. Qt
  WebEngine uses Chromium's spell checker, which reads `.bdic` and cannot read
  hunspell's `.dic`/`.aff` at all, so the language list was empty on every
  distribution. The 31 dictionaries already in the tree are now converted at
  build time and installed.
* **Chat themes.** Fourteen of them, recolouring WhatsApp Web itself. Keyed on
  the colour *values*, since WhatsApp's CSS variable names are
  compiler-generated and change with each of its deploys. Photos and avatars are
  untouched.
* **Chat wallpaper** — your own image behind the messages.
* **Custom CSS.** Load a .css file to restyle WhatsApp Web — the community
  stylesheets (catppuccin and the like) work — applied on top of the chat theme.
* **Smooth scrolling**, as an option.
* **Privacy blur.** Blurs the chat list and the open conversation until you
  hover a row, so someone glancing at the screen cannot read them. Five levels.
* **Theme and blur buttons inside WhatsApp's own sidebar**, above the avatar,
  reachable without opening Settings.
* **Interface translations**, with a language picker. 15 languages (Italian was
  human-written; the other 14 are machine-translated and unreviewed).
* **Windows support**, behind `Q_OS` guards, with a build workflow.
* Image paste from a browser's clipboard.
* A connection watchdog that reloads the page when WhatsApp's WebSocket hangs,
  capped at three reloads per episode.
* Identify as Whatly in the phone's linked-devices list, instead of as Chrome —
  reported as a desktop client so the phone shows a computer icon beside the
  "Whatly for Linux" label rather than leaving it blank.
* Close the emoji panel by clicking outside it (opt-in).
* `F1` opens About; its **Report a Bug** button fills in the GitHub issue with
  the version, commit, memory usage of the whole process tree, and the recent
  log — including WhatsApp Web's console.

#### 🐞 Bug Fixes

* **Logging out of KDE could stall on Whatly.** With close-to-tray on, the
  window vetoed *every* close — including the one the session manager sends at
  logout — so the desktop saw an app refusing to quit and waited on it. A
  session-end close is now honoured as a real quit. (Reported on KDE; the fix
  hooks `QGuiApplication::commitDataRequest`, which fires only on a real session
  logout, so it could not be exercised in a normal run.)
* **Clearing the cache could delete your home directory.** If the profile ever
  handed back an empty storage path, the recursive delete ran on `.` — the
  working directory, which is your home when the app is launched from a desktop
  or file manager. The delete now refuses any path that is empty, relative, the
  home directory, the root, or not inside the app's own storage. (Gentoo dropped
  the app over this.)
* **The theme could not be set to dark in any language but English.** The
  setting was stored as the combo box's *displayed* text, which is translated:
  running in Spanish wrote `windowTheme=claro`, and every comparison in the code
  is against `"dark"`. A value written by an older build is repaired at startup.
* **The permissions dialog did nothing at all** — wrong enum, a double-prefixed
  settings key, and a signal nothing was connected to. It never once reached the
  engine. This is also why voice and video calls appeared not to work: the
  microphone and camera could not be granted.
* **Notifications went to the app's own popup, on the primary monitor**, rather
  than to the desktop's notification service and the screen the window is on.
* Notification avatars had red and blue swapped (a byte-order mismatch between
  `QImage` and the freedesktop `image-data` hint).
* The window would not restore down from maximized (Wayland reports the
  maximized geometry as the normal one).
* The theme was reset to light on every exit.
* Pasting an image copied from a browser silently produced "no content".
* Attachments: the desktop's file chooser is used, and the last directory is
  remembered. Qt's built-in dialog has no bookmarks, no Recent and no address
  bar, so a file outside the directory it happened to open in was unreachable.
* Quitting could turn into minimize-to-tray.
* "Restore" in the tray menu could be left permanently disabled, with no way
  left to bring the window back.
* Logging out hung on the "Logging out" overlay and opened a browser tab.
* Unhandled permission types were denied *and the denial was persisted*, so they
  stayed denied forever once the app learned to ask.
* The injected sidebar buttons burned about 40% of a CPU core with the app
  idle — a MutationObserver whose own repaints retriggered it.
* The User-Agent is derived from the engine, so it always reports the truth.

#### 📦 Packaging (snap)

* Stage `libxcb-shape0`: `libqxcb` links against it, and without it the snap
  does not launch at all.
* Drop the `hunspell-dictionaries` content mount and `DICPATH`. It could never
  have fed the spell checker — Chromium reads `.bdic`, not hunspell's
  `.dic`/`.aff` — and the dictionaries are shipped inside the snap now.

#### 📖 Documentation

* The build docs describe the actual CMake build. Old Qt or CMake now fails with
  an actionable error instead of a confusing one.
* `docs/TRANSLATIONS.md`, `docs/WINDOWS_BUILD.md`.

#### ⚠️ Known limitations

* **Screen sharing during a call does not work.** Qt WebEngine as packaged
  enumerates zero screens and zero windows (measured, on Wayland and on X11
  alike); it is built without PipeWire. Nothing in the application can change
  that.
* Memory use is Chromium's, not the app's: with every injected script disabled,
  the renderer still accounts for the great majority of it.

## 5.1.0 (2026-04-03)

#### 🎁 Feature

* migrate build system workflow to CMake + Ninja for Qt 6 builds

#### 🐞 Bug Fixes

* improve WebEngine profile/session persistence handling on Qt 6
* update in-app theme application logic for current WhatsApp Web changes
* show notifications through org.freedesktop.Notifications directly on Qt 6

#### 🚧 Chores

* align release metadata for minor version bump to 5.1.0

## 4.16.0 (2024-10-09)

#### 🎁 Feature

* Add secure compilation flags (4720ffeb)

#### 🐞 Bug Fixes

* change zoom factor when app starts minimized in systray (be47a73d)
* set QTWEBENGINE_DICTIONARIES_PATH to fix spell checker (#199) (40da519b)

## 4.15.1 (2024-08-01)

#### 🚧 Chores

* revert to build action Qt to version 5.15.2 (bbccc551)
* bump version 4.15.1 (d8746929)
* update dist icon (d0304ad1)
* new icon (f54e608c)
* update notification icons (2718a85d)
* add new keywords to appdata (4e803bfa)
* update appstream metadata for 4.15.0 release (a535ee1b)
* do not mark statusCode unused in desktopOpenUrl util method (51e28237)
* udapte CHANGELOG (5940b6ec)


## 4.15.0 (2024-05-25)

#### 🎁 Feature

* efficient use of element mutation observers (e553c00c)
* add line widget at bottom of cutom notification widget (0d19713d)
* use xdg-open as fallback to open local files (30921582)

#### 🐞 Bug Fixes

* restore window state video fullscreen toggle (e0483dda)
* prioritize xdg-open for handling openUrl request (36cd46b7)
* set correct lock state when app starts minimized (b58d9422)

#### 🚧 Chores

* CI Update Qt version to 5.15.4 for build (e030486b)
* update UA to latest Chrome (cc2852a7)
* snap use portal if available (8cef2047)
* version 4.15 (08fba4cf)

## 4.14.2 (2023-12-01)

#### 🐞 Bug Fixes

* incorrect full width modification (#150) (3d20ebe2)

#### 🚧 Chores

* bump version 4.14.2 (c478a7d6)
* Use qmake-provided _DATE_ (#146) (fec5644c)



## 4.14.1 (2023-05-20)

#### 🐞 Bug Fixes

* fix unread message parsing (906ca7eb)

#### 🚧 Chores

* update workflow and changelog (41225b19)



## 4.14.0 (2023-05-17)

#### 🎁 Feature

* minor fixes + code cleanup (5f10a0f9)

#### 📄 Documentation

* **changelog:** update cl & ver after release (b4b5dc33)

#### 🚧 Chores

* bump version to 4.14 (c235b8ae)


## 4.13.0 (2023-03-22)

#### 🎁 Feature

* add open file option in download item (7b64cf51)

#### 🐞 Bug Fixes

* prevent overwrite if file exists (dd687dc9)

#### 📄 Documentation

* **changelog:** update cl & ver after release (fa5add01)

#### 🚧 Chores

* bump version 4.13 (3484bccc)
* update js to improve perf (1b030ca7)
* code cleanup (c2bd1a32)
* update utils (8f983031)


## 4.12.1 (2023-01-27)

#### 🐞 Bug Fixes

* icon on Plasma Wayland (#100) (7fc4ce38)

#### 📄 Documentation

* **changelog:** update cl & ver after release (4417eced)

#### 🚧 Chores

* code cleanup (085205eb)
* cleanup (011db449)
* escape key to close child windows (56c55a94)


## 4.12.0 (2023-01-26)

#### 🎁 Feature

* close permission dialog with esc key (ee519bcc)
* close with esc button (2119c3d1)

#### 🐞 Bug Fixes

* prevent zoom with ctrl+mouse (0eb7ea05)

#### 📄 Documentation

* **changelog:** update cl & ver after release (b3e2a2be)

#### 🚧 Chores

* bump version 4.12 (12bce6d2)
* cleanup (c78394d1)


## 4.11.0 (2023-01-26)

#### 🎁 Feature

* bump version 4.11 (63440d96)

#### 🐞 Bug Fixes

* applock state fix (32b0e5ca)

#### 📄 Documentation

* **changelog:** update cl & ver after release (74216cfd)

#### 🚧 Chores

* remove debugging from MoreApps widget (103a3686)
* cleanup + inhancements (f31197a4)
* cleanup + addition (21ca36bd)


## 4.10.3 (2022-12-18)

#### 📄 Documentation

* **changelog:** update cl & ver after release (d7f1faee)

#### 🚧 Chores

* add moreapps widget in lock screen (074b0f98)


## 4.10.2 (2022-09-20)

#### 📄 Documentation

* **changelog:** update cl & ver after release (3a71de33)

#### 🚧 Chores

* make description under 1000 chars (1c5bfc42)


## 4.10.1 (2022-09-17)

#### 📄 Documentation

* **changelog:** update cl & ver after release (56c06a92)

#### 🚧 Chores

* update appdata, to make flatpak-builder happy (c8b1b838)


## 4.10.0 (2022-09-17)

#### 🎁 Feature

* systemtray notification counter (530c24bf)
* add toggle theme desktop action (f8c9b339)
* **ci:** add release workflow (83cd6383)
* **i18n:** add Italian localization (#55) (ced5547d)
* enable support for traybar entries on GNOME dash (#53) (66d20d3e)
* some new features (21113900)
* unlock animation plus some cleanup (0a182a9e)
* implement IPC & other improvements (81faa022)
* add open downloads directory button in download widget (419ffb29)
* app auto locking (d06a4abb)
* v4.0 (#35) (474b9212)
* start application minimized. closes #19 (c5bf7a98)

#### 🐞 Bug Fixes

* duplicate action in desktop file (2837c87e)
* auto lock while scrolling (baa52666)
* **build:** fix build due to missing icon (5464060d)
* focus on password edit when echo (cee2dc85)
* **web:** bypass lock check while loading quirk (6c6275c3)
* obey fullview settings on first launch & initial window size (b2f0fe49)
* properly hide custom notification on multi monitor setups (20057675)
* use availableGeometry to map position of notification (538d7d5d)
* add missing icon, enabling install_icon target generation (clos… (#45) (48b9028f)
* show notifications on correct screen (ff99a5f7)
* logout flow during changepassword (92382d7b)
* properly load setting for autoapplock checkbox (522eb75a)
* save geometry in quit event (4a968554)
* raise window from hidden state when clicked on notification (0620e43e)
* debug in debug mode (147487f2)
* notification popup click behavior (e800208f)
* **snap:** supress warnings (f2b06da6)
* improve logout flow, on change password (ed5f760b)
* change lock screen password beahvior (fa4012a5)
* theme switching (7cd4b219)
* improve download file behavior (#32) (8f071469)

#### 📄 Documentation

* **changelog:** update cl & ver after release (1f2bb6fc)
* **changelog:** update cl & ver after release (5cdba515)
* **changelog:** update cl & ver after release (9472e9e6)
* **changelog:** update cl & ver after release (bd7386a1)
* **changelog:** update cl & ver after release (554ceff4)
* **changelog:** update cl & ver after release (572d6948)
* **changelog:** update cl & ver after release (e0d15c2e)
* **changelog:** update cl & ver after release (974933d0)

#### 🎨 Styles

* code refactor (21940ee6)

#### 🚧 Chores

* update appdata (273a9138)
* **snap:** use svg icon for snap store (bcec3eef)
* skip dictionaries conversion if build type is flatpak build (f16086c2)
* Version bump 4.9 (af39ff62)
* **snap:** use svg icon for desktop entry (51dcb0d4)
* **ci:** release dry-run action (7ef46aaf)
* delete filedialog after exec (5e50519c)
* add notification icons (997ae821)
* **webengine:** disable support for Pepper plugins (325d841e)
* some enhancements (ff575c45)
* update UA to 104 (d03e9fc6)
* bump version 4.8 (7fde1e4c)
* unlock action button (771625da)
* **build:** use Qt5.15.4 for build (54f97210)
* update readme (0d3bd466)
* udapte new settings window screenshot (6f3f18c6)
* remove unused xml module (2d71f12c)
* version bump in pro (211699e3)
* **ci:** disable auto release and version file (6f134db2)
* Merge branch 'dev' (9f566869)
* **ui:** update ui color (8a74ccbc)
* use pre-commit (f82dcc68)
* update todo (2aa08e03)
* setQuitOnLastWindowClosed false (c751be26)
* set a minimum of 4 digits for the lock code (#56) (79b2b791)
* notification connect before show (f8455de7)
* update app description (c6fd2e8d)
* use appinstall artifacts from dist (247ed75f)
* distribution related files (88c46fad)
* **CI:** use latest version of install-qt-action (60b6c225)
* **CI:** build with github action (ac31abdb)
* define fallback values for macros (14f190c0)
* **qmake:** avoid error message when .git folder is missing (close #49) (#52) (91d0cf11)
* add full view support closes #46 (b96a28db)
* version 4.4 (26f5659b)
* install dicts using qmake (90210de2)
* add git sponser link (122828f4)
* improve settings window show behavior (d9909011)
* improve window geo restore (3a08d5d5)
* nitification popup tweak; code cleanup (5c2764f7)
* update readme (a4c73b0f)
* version 4.3 (3dae93a1)
* use Ctrl+W to hide window to tray (dba5a9bc)
* filter contextmenu items (6f4750c8)
* restore window directly when another instance is launched (39117158)
* use new chat trigger method to invoke new chats (1d950cd8)
* update changelog (59abd9d9)
* version 4.2 (1f4816a2)
* remove runguard (8c0df6d3)
* window show behavior (7d302466)
* update default UA (dfb5b9ca)
* stop timer instantly if rated already (cc43d4c7)
* bump version 4.1 (a1af1bde)
* minor improvements (ea4056dc)
* clean UA & disable js debug in app stdout (8cfbcf4b)
* set default zoom factor for maximized windows to 1.0 (046e2e13)
* inform app is minimized via notification (19734a99)
* unify passowrd echomode in lock widget (5be4cae9)
* test qpt gtk3 (020ac6da)
* add Desktop entry GenericName (e4bbdd15)
* move desktop file to src (4f0558a9)
* use desktop-launch from content snap (dcc39239)

#### 📦 Build

* **snap:** use SNAPCRAFT_ARCH_TRIPLET (8962c8bb)
* migrate to qt 5.15 (9867a6b6)

#### chaore

* **CI:** use Qt 5.15.2 (846d1218)

#### cleanup

* removed snap_launcher (e658c464)


## 4.9.1 (2022-09-13)

#### 📄 Documentation

* **changelog:** update cl & ver after release (5cdba515)

#### 🚧 Chores

* skip dictionaries conversion if build type is flatpak build (f16086c2)


## 4.9.0 (2022-09-03)

#### 🎁 Feature

* systemtray notification counter (530c24bf)

#### 📄 Documentation

* **changelog:** update cl & ver after release (9472e9e6)

#### 🚧 Chores

* Version bump 4.9 (af39ff62)
* **snap:** use svg icon for desktop entry (51dcb0d4)
* **ci:** release dry-run action (7ef46aaf)
* delete filedialog after exec (5e50519c)
* add notification icons (997ae821)
* **webengine:** disable support for Pepper plugins (325d841e)
* some enhancements (ff575c45)
* update UA to 104 (d03e9fc6)


## 4.8.2 (2022-08-27)

#### 🐞 Bug Fixes

* duplicate action in desktop file (2837c87e)
* auto lock while scrolling (baa52666)

#### 📄 Documentation

* **changelog:** update cl & ver after release (bd7386a1)


## 4.8.1 (2022-08-27)

#### 🐞 Bug Fixes

* **build:** fix build due to missing icon (5464060d)

#### 📄 Documentation

* **changelog:** update cl & ver after release (554ceff4)

#### 🚧 Chores

* bump version 4.8 (7fde1e4c)


## 4.7.2 (2022-07-22)

#### 📄 Documentation

* **changelog:** update cl & ver after release (e0d15c2e)


## 4.7.1 (2022-07-04)

#### 🐞 Bug Fixes

* focus on password edit when echo (cee2dc85)
* **web:** bypass lock check while loading quirk (6c6275c3)

#### 📄 Documentation

* **changelog:** update cl & ver after release (974933d0)

#### 🚧 Chores

* Merge branch 'dev' (9f566869)
* **ui:** update ui color (8a74ccbc)


## 4.7.0 (2022-07-03)

#### 🎁 Feature

* **ci:** add release workflow (83cd6383)
* **i18n:** add Italian localization (#55) (ced5547d)
* enable support for traybar entries on GNOME dash (#53) (66d20d3e)
* some new features (21113900)
* unlock animation plus some cleanup (0a182a9e)
* implement IPC & other improvements (81faa022)
* add open downloads directory button in download widget (419ffb29)
* app auto locking (d06a4abb)
* v4.0 (#35) (474b9212)
* start application minimized. closes #19 (c5bf7a98)

#### 🐞 Bug Fixes

* obey fullview settings on first launch & initial window size (b2f0fe49)
* properly hide custom notification on multi monitor setups (20057675)
* use availableGeometry to map position of notification (538d7d5d)
* add missing icon, enabling install_icon target generation (clos… (#45) (48b9028f)
* show notifications on correct screen (ff99a5f7)
* logout flow during changepassword (92382d7b)
* properly load setting for autoapplock checkbox (522eb75a)
* save geometry in quit event (4a968554)
* raise window from hidden state when clicked on notification (0620e43e)
* debug in debug mode (147487f2)
* notification popup click behavior (e800208f)
* **snap:** supress warnings (f2b06da6)
* improve logout flow, on change password (ed5f760b)
* change lock screen password beahvior (fa4012a5)
* theme switching (7cd4b219)
* improve download file behavior (#32) (8f071469)

#### 🎨 Styles

* code refactor (21940ee6)

#### 🚧 Chores

* use pre-commit (f82dcc68)
* update todo (2aa08e03)
* setQuitOnLastWindowClosed false (c751be26)
* set a minimum of 4 digits for the lock code (#56) (79b2b791)
* notification connect before show (f8455de7)
* update app description (c6fd2e8d)
* use appinstall artifacts from dist (247ed75f)
* distribution related files (88c46fad)
* **CI:** use latest version of install-qt-action (60b6c225)
* **CI:** build with github action (ac31abdb)
* define fallback values for macros (14f190c0)
* **qmake:** avoid error message when .git folder is missing (close #49) (#52) (91d0cf11)
* add full view support closes #46 (b96a28db)
* version 4.4 (26f5659b)
* install dicts using qmake (90210de2)
* add git sponser link (122828f4)
* improve settings window show behavior (d9909011)
* improve window geo restore (3a08d5d5)
* nitification popup tweak; code cleanup (5c2764f7)
* update readme (a4c73b0f)
* version 4.3 (3dae93a1)
* use Ctrl+W to hide window to tray (dba5a9bc)
* filter contextmenu items (6f4750c8)
* restore window directly when another instance is launched (39117158)
* use new chat trigger method to invoke new chats (1d950cd8)
* update changelog (59abd9d9)
* version 4.2 (1f4816a2)
* remove runguard (8c0df6d3)
* window show behavior (7d302466)
* update default UA (dfb5b9ca)
* stop timer instantly if rated already (cc43d4c7)
* bump version 4.1 (a1af1bde)
* minor improvements (ea4056dc)
* clean UA & disable js debug in app stdout (8cfbcf4b)
* set default zoom factor for maximized windows to 1.0 (046e2e13)
* inform app is minimized via notification (19734a99)
* unify passowrd echomode in lock widget (5be4cae9)
* test qpt gtk3 (020ac6da)
* add Desktop entry GenericName (e4bbdd15)
* move desktop file to src (4f0558a9)
* use desktop-launch from content snap (dcc39239)

#### 📦 Build

* **snap:** use SNAPCRAFT_ARCH_TRIPLET (8962c8bb)
* migrate to qt 5.15 (9867a6b6)

#### chaore

* **CI:** use Qt 5.15.2 (846d1218)

#### cleanup

* removed snap_launcher (e658c464)


## 4.6.5 (2022-07-03)

#### 🚧 Chores

* **ci:** update release wf (#59) (f40ac9c9)


## 4.6.3 (2022-07-03)

#### 📄 Documentation

* **changelog:** update changelog after release (7699d885)

#### 🚧 Chores

* **ci:** fix update file name (c0158c0d)


## 4.6.2 (2022-07-03)

#### 📄 Documentation

* **changelog:** update changelog after release (c87524db)

#### 🚧 Chores

* **ci:** update version on release (d715c8eb)


## 4.6.1 (2022-07-03)

#### 🚧 Chores

* **CI:** commit changelog on release (75b0cffe)


## Change log:

### 4.3
- feat: IPC; restore window directly when another instance is launched
- feat: allow context menu on editable, selected and copyble data types
- fix: properly load setting for autoapplock checkbox
- fix: logout flow during changepassword
- fix: the minimize behavior; replace Ctrl+H with Ctrl+W to hide window to tray

### 4.2
- fix: raise window from hidden state when clicked on notification
- updated new UA
- fix: window geometry persistence behavior
- feat: open download directory straight from the download manager
- fix: consistent window show behavior
- feat: implement IPC
   - lets run only one instance of application
   - lets pass arguments from secondary instances to main instance
   - open new chat without reloading page
   - restore application with command line argument to secondary instance:
          example: `whatly whatsapp://whatly`
          will restore the primary instance of whatly process

### 4.0
- fix(SystemTray) tray icon uses png rather than SVG
- feat(SystemTray) added settings to lets users change the system tray icon click behavior(minimize/maximize on right-click)
- feat(Download) added setting that lets the user set default download directory, avoid asking while saving files
- fix(Notification) clicking popup now correctly restores the app window
- feat(Lock) added setting to let users change the current set password for the lock screen
- feat(Lock) added setting to enable disable auto app locking, with defined duration
- feat(Lock) current set password is now hidden by default and can be revealed for 5 seconds by pressing the view button
- feat(Style/Theme) added ability to change widget style on the fly, added default light palette (prevent breaking of light theme on KDE EVs)
- fix(Theme) dark theme update
- feat(WebApp) added setting to set zoom factor when the window is maximized and fullscreen (gives user ability to set different zoom factor for Normal, Maximized(Fullscreen WindowStates)
- fix(Setting) settings UI is more organized
- fix(WebApp) enable JavaScript execCommand("paste")
- feat(WebApp) tested for new WhatsApp Web that lets users use Whatly without requiring the phone connected to the internet
- fix(Lock) unify passowrd echomode in lock widget


