# urfd has moved

This repository is archived. urfd, the tcd transcoder and the setup script now live at

## https://github.com/jcmerg/urfd-ng

```bash
git clone https://github.com/jcmerg/urfd-ng.git urfd && cd urfd && sudo ./setup.sh
```

**Existing installations:** `sudo ~/urfd/setup.sh update` switches a checkout of this repository over to urfd-ng by itself. Run it twice if your checkout is older than October 2026 (the first run fetches the version that knows about the move). Or switch by hand:

```bash
cd ~/urfd && git remote set-url origin https://github.com/jcmerg/urfd-ng.git && git pull
```

The full history is in urfd-ng, and the old code remains on the `main` branch here.

---

# urfd ist umgezogen

Dieses Repository ist archiviert. urfd, der Transcoder tcd und das Setup-Script liegen jetzt unter **https://github.com/jcmerg/urfd-ng**.

**Bestehende Installationen:** `sudo ~/urfd/setup.sh update` stellt einen Klon dieses Repositorys selbst auf urfd-ng um. Ist dein Klon älter als Oktober 2026, führe es zweimal aus (der erste Lauf holt die Version, die vom Umzug weiß). Oder per Hand:

```bash
cd ~/urfd && git remote set-url origin https://github.com/jcmerg/urfd-ng.git && git pull
```

Die komplette Historie steckt in urfd-ng, der alte Code bleibt hier im Branch `main`.
