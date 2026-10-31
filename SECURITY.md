# Security

This is a hobby project. It has no inbound network services, but it has known limits that you should understand before you use it.

## Known limitations

| Limitation | Who it affects | Risk | What you can do |
|---|---|---|---|
| **Gmail's certificate is not verified.** The email library connects to `smtp.gmail.com:465` over TLS but its default is `certificate.verify = false`, so the board does not check that the server is really Google. | Anyone running the board on a network they don't fully control (shared flat, office, café) | Someone who can tamper with traffic on that network could capture the sender account's app password | Use a **throwaway sender account**, never your main one. Run the board on your own home network. Revoke the app password if you suspect a problem |
| **Secrets are stored in the board's flash.** The Gmail app password is compiled into the firmware, and the Wi-Fi password is saved by WiFiManager. Flash encryption is not enabled. | Anyone who physically holds the board | They can read both passwords with common tools | Keep the board in a place you control. Use the throwaway account. Change your Wi-Fi password if the board is lost |
| **The setup hotspot is open.** While the board has no saved Wi-Fi it opens `ESP32-CAM-Setup` without a password. | Anyone physically in range during first setup | They could join it and enter their own Wi-Fi details | Do first-time setup at home, quickly. After setup the hotspot is closed |
| **Alert emails reveal presence.** Each email says when a person was seen. | Whoever can read the receiving inbox | They learn when someone is in view | Protect that mailbox with 2-Step Verification |
| **No firmware updates over the air, no authentication on the device.** | Anyone with a USB cable and physical access | Can re-flash the board | Physical security |

What this device does **not** do: it has no web server, no open ports, does not stream or store images, and does not send video anywhere.

## Do not commit secrets

`PersonAlert/secrets.h` holds your private values and is listed in `.gitignore`. Before every push run `git status` and make sure it is not listed. If you ever commit a password by mistake, revoke it first (Google Account > Security > App passwords), then clean the history.

## Reporting a vulnerability

Please do not open a public issue for a security problem. Use GitHub's private vulnerability reporting on this repository (Security tab > Report a vulnerability). I will reply as time allows; this is a spare-time project.

## Possible hardening (not done yet)

- Pin Google's root certificate and set `certificate.verify = true` in `sendAlert()`.
- Enable ESP32 flash encryption (a one-way eFuse change, so only for dedicated boards).
- Password-protect the setup hotspot.
