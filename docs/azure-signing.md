# Wiring up Azure Trusted Signing for Pixee

Working notes for turning on code-signing so the installer stops tripping
SmartScreen. The company already has an **Azure Trusted Signing** account (set
up by the other founder); this is what's needed to actually use it.

> **Status:** not wired yet. Blocked on gathering the four values + the role
> grant below. Once those exist, follow "What gets wired up" to finish it.
> The installer already has the hooks in place (`installer/Pixee.iss` has the
> commented `SignTool=azuresign` lines; `build-installer.bat` is where the
> app-exe sign step slots in). See also `docs/installer.md` Part 3.

## How Trusted Signing works (mental model)

There is **no private key to download** — that's the whole point. The signing
key lives in Microsoft-managed HSMs (FIPS 140-2 Level 3) and never leaves.
Signing is a *service call*:

1. Your machine hashes `Pixee.exe`.
2. The signing tool authenticates to the Trusted Signing endpoint with your
   Azure identity.
3. It sends only the **hash** (the binary never leaves your machine); the
   service signs it with the HSM key using a freshly issued **short-lived
   certificate** (~3-day validity).
4. The tool embeds the signature + cert chain and adds an RFC-3161
   **timestamp** — the timestamp is what keeps the signature valid long after
   that 3-day cert expires.

So there's no long-lived cert to store or renew, and nothing secret on disk.
The "secret" is just *your permission to call the service*.

## What to get from the other founder

Read straight off the Azure portal → the Trusted Signing account:

1. **Account name** — the Trusted Signing account resource name.
2. **Certificate profile name** — under that account. This is the identity that
   becomes "signed by ⟨company⟩" in the file's Digital Signatures tab.
3. **Region / endpoint** — either the endpoint URL directly
   (e.g. `https://eus.codesigning.azure.net`) or just the region
   (e.g. "East US") and we form the URL.
4. **Confirm the profile status is "Completed"** — i.e. the org identity
   validation actually finished. If it's still pending, signing fails no matter
   what else is set up.

### Plus one access grant — pick a path

**Path A — your own Azure login (simplest; recommended for signing on your PC):**
- Have them assign **your** Entra (Azure AD) account the role
  **"Trusted Signing Certificate Profile Signer"** on the account (or profile).
- Then locally: `az login` once. Nothing secret to store.

**Path B — service principal (only if CI / GitHub Actions should sign):**
- An app registration granted the same **Signer** role. You'd receive:
  **tenant ID**, **client ID**, and a **client secret** (or a federated
  credential).

For a two-person company signing on your own machine, **Path A** is the easy
one. Use Path B only when automated release builds are wanted.

### What is NOT needed (and can't be handed over)

No private key, no `.pfx`, no cert file, no password — there isn't one. The key
stays in Microsoft's HSM; **access is the credential.**

**Minimum to unblock:** account name, profile name, region, and your account
added to the Signer role.

## What gets wired up (the plan for tomorrow)

1. **`installer/sign.cmd`** (new) — wraps Microsoft's Sign CLI. Reads
   `account` / `profile` / `endpoint` from **environment variables** (nothing
   secret committed) and **no-ops cleanly when they're unset**, so normal
   unsigned local builds keep working. Signs whatever file path it's handed.
2. **`build-installer.bat`** — add a step that signs `Pixee.exe` *before* Inno
   packs it, so the installed exe is signed (not just `setup.exe`).
3. **`installer/Pixee.iss`** — uncomment `SignTool=azuresign` and
   `SignedUninstaller=yes`, and register a signer named `azuresign` (Inno:
   Tools → Configure Sign Tools) that points at `sign.cmd`. Inno then signs
   `setup.exe` + the uninstaller automatically.

Net effect: no SmartScreen warning, and the file properties show the company as
publisher. Sign **both** `Pixee.exe` and `setup.exe`; always timestamp; the
bundled third-party codec DLLs don't need our signature.

## Prerequisites (no need to ask anyone — set up when we do the work)

- **Sign CLI:** `dotnet tool install --global sign` (needs the .NET runtime).
- **Azure CLI:** for `az login` (Path A) — `winget install Microsoft.AzureCLI`.

## The command it comes down to

Once the values exist, the core call (from `docs/installer.md` Part 3):

```cmd
sign code trusted-signing ^
  --trusted-signing-account            <account-name> ^
  --trusted-signing-certificate-profile <profile-name> ^
  --trusted-signing-endpoint           https://<region>.codesigning.azure.net ^
  --description "Pixee" ^
  --timestamp-url http://timestamp.acs.microsoft.com ^
  "Pixee-portable\Pixee.exe"
```

`sign.cmd` will just be this with the three identifiers pulled from env vars.

## References

- `docs/installer.md` — Part 3 (signing) and the overall release pipeline.
- Microsoft Sign CLI: https://github.com/dotnet/sign
- Azure Trusted Signing docs: https://learn.microsoft.com/azure/trusted-signing/
