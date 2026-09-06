# Signing the AppImage (issue #85)

The release AppImage can carry an embedded GPG signature, so the self-update
path (`appimageupdatetool`, offered by the in-app **Update now**) fetches an
image whose origin can be verified rather than trusting HTTPS alone.

Signing is **optional and off until a key is configured**: with no
`APPIMAGE_GPG_PRIVATE_KEY` repository secret, `release-artifacts.yml` builds the
AppImage unsigned exactly as before. When the secret is present, `appimagetool`
(invoked by `linuxdeploy --output appimage`) embeds a detached signature
(`SIGN=1`, `SIGN_KEY=<fingerprint>`), and the workflow publishes the public half
as `whatly-appimage-pubkey.asc` beside the image.

## One-time: generate the signing key

Use a **dedicated, passphrase-less** key for CI — `appimagetool` drives `gpg`
itself and cannot answer a passphrase prompt. Keep it separate from any personal
key.

```bash
# 0. Run everything from the repository root (the relative paths below assume it).
cd /path/to/whatly

# 1. Generate the key non-interactively, no passphrase, no expiry.
cat > /tmp/whatly-appimage-key.conf <<'EOF'
%no-protection
Key-Type: EDDSA
Key-Curve: ed25519
Key-Usage: sign
Name-Real: Whatly AppImage Signing
Name-Email: angel@guzmanmaeso.com
Expire-Date: 0
%commit
EOF
gpg --batch --gen-key /tmp/whatly-appimage-key.conf
rm -f /tmp/whatly-appimage-key.conf

# 2. Find the fingerprint. Select by the unique Name-Real, NOT the email — the
#    maintainer already has personal keys under angel@guzmanmaeso.com, and
#    selecting by email would match those (and fail to export, since they carry
#    a passphrase). This picks only the new, passphrase-less signing key.
KEYFPR=$(gpg --list-secret-keys --with-colons 'Whatly AppImage Signing' \
  | awk -F: '/^fpr:/{print $10; exit}')
echo "signing key: $KEYFPR"

# 3. Export the PRIVATE key by fingerprint (this is the CI secret's value).
gpg --armor --export-secret-keys "$KEYFPR" > whatly-appimage-private.asc

# 4. Export the PUBLIC key (commit this next to this file; also published per release).
gpg --armor --export "$KEYFPR" > packaging/appimage/whatly-appimage-pubkey.asc
```

## One-time: add the CI secret

Add the private key as a repository secret named **`APPIMAGE_GPG_PRIVATE_KEY`**
(paste the whole contents of `whatly-appimage-private.asc`, including the
`-----BEGIN/END PGP PRIVATE KEY BLOCK-----` lines):

```bash
cd /path/to/whatly   # where whatly-appimage-private.asc was written
gh auth switch --user shakaran
gh secret set APPIMAGE_GPG_PRIVATE_KEY --repo shakaran/whatly \
  < whatly-appimage-private.asc
shred -u whatly-appimage-private.asc   # or: rm -f
```

`--repo shakaran/whatly` is required: this checkout also has an `upstream`
remote, and without it `gh` targets the wrong repository. `gh secret set` needs
the `shakaran` account active (`gh auth switch --user shakaran`). Delete the
local
`whatly-appimage-private.asc` afterwards — GitHub stores it write-only, and it
should not stay on disk or in the tree.

Finally commit the public key:

```bash
cd /path/to/whatly
git add packaging/appimage/whatly-appimage-pubkey.asc
git commit -m "build(appimage): add the AppImage signing public key (#85)"
git push origin main
```

## Verifying a release AppImage by hand

```bash
gpg --import packaging/appimage/whatly-appimage-pubkey.asc   # once
./Whatly-<ver>-x86_64.AppImage --appimage-signature          # show the signature
# or validate with appimagetool if installed:
appimagetool --validate Whatly-<ver>-x86_64.AppImage
```

## Still to do

- **In-app verification before self-update.** The updater currently runs
  `appimageupdatetool` and offers to restart; it does not yet verify the new
  image's signature against the committed public key before restarting into it.
  That is the remaining security step and wants a real signed release to develop
  and test against (which is why it is not guessed at here).
