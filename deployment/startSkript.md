# Betrieb, Boot und atomare Updates

Dieses Dokument beschreibt den Produktionsbetrieb auf Raspberry Pi OS Bookworm mit X11. Beide
Anwendungen laufen als direkt von systemd überwachte Prozesse. Builds entstehen in einem getrennten
Git-Worktree, während das bisherige Release weiterläuft. Erst nach erfolgreichem Build wird kurz auf
ein vollständiges, versioniertes Release unter `/opt/thelastgameofmoyn` umgeschaltet.

## Verwaltete Pfade

| Pfad | Eigentümer | Inhalt |
|---|---|---|
| `/home/kathapult/thelastgameofmoyn` | `kathapult` | Git-Checkout; Quelle für Fetch und getrennte Build-Worktrees |
| `/opt/thelastgameofmoyn/releases/<app>/<commit>` | `root` | unveränderliches Binary, `data/` und zugehörige Deploymentdateien |
| `/opt/thelastgameofmoyn/current/<app>` | `root` | atomar gewechselter Symlink auf das aktive Release |
| `/var/lib/thelastgameofmoyn/sensorApp/settings.json` | `kathapult` | dauerhafte, nicht von Git oder Rollback berührte Sensorkalibrierung |
| `/var/lib/thelastgameofmoyn/sensorApp/table-surface-mask.bin` | `kathapult` | optionales, per Kalibrierungsmodus aufgenommenes Tiefenmodell der leeren Tischfläche |
| `/var/lib/thelastgameofmoyn/visualApp/projection-calibration.json` | `kathapult` | dauerhafte Maße, Grundseite, Projektionslage und Kinect-Achsenabbildung |
| `/var/lib/thelastgameofmoyn/update/<app>` | `root` | aktiver und zuletzt betrachteter Commit sowie Update-Lock |
| `/etc/thelastgameofmoyn/<app>.env` | `root` | standortabhängige X11-, Netzwerk- und Beamerkonfiguration |
| `/etc/thelastgameofmoyn/<app>.env.example` | `root` | Referenz mit den Defaults des zuletzt installierten Releases |
| `/etc/thelastgameofmoyn/internet-sharing.conf` | `root` | nur nach einem ausdrücklichen `--apply`: Rollenzuordnung und Rücksetzwerte der optionalen Internetfreigabe |
| `/etc/thelastgameofmoyn/internet-sharing.nft` | `root` | nur nach einem ausdrücklichen `--apply`: die verwalteten Forwarding-/NAT-Regeln zwischen `eth0` und `wlan0` |

Die Hauptdienste verwenden das aktive Release als `WorkingDirectory`. Relative openFrameworks-Pfade
zeigen dadurch weiterhin auf dessen `data/`-Verzeichnis. Im Sensor-Release ist
`data/settings.json`, im Visual-Release `data/projection-calibration.json` jeweils ein Symlink auf
die dauerhafte Datei unter `/var/lib`. Pi A löst das Ziel des Settings-Symlinks auf und speichert
`table-surface-mask.bin` daneben, sodass auch das Tiefenmodell Releasewechsel überlebt.

## Voraussetzungen

- Das Repository liegt auf beiden Pis unter `/home/kathapult/thelastgameofmoyn` und besitzt ein
  Remote `origin` mit Branch `main`.
- Der Benutzer `kathapult` kann das Repository lesen, `git fetch` ausführen und die jeweilige App
  mit `make Release` bauen.
- Raspberry Pi OS startet für `kathapult` eine X11-Sitzung. `DISPLAY` und `XAUTHORITY` werden nicht
  im Programm erraten, sondern in `/etc/thelastgameofmoyn/<app>.env` festgelegt.
- Pi B besitzt `/usr/bin/xrandr`; `/usr/bin/xset` wird, falls vorhanden, zum Abschalten von
  Screensaver und DPMS verwendet.
- Pi B verwendet den KMS-Treiber. Der feste Projektormodus wird über einen `video=...D`-Parameter
  in `/boot/firmware/cmdline.txt` erzwungen, damit auch ein beim Boot ausgeschalteter Beamer einen
  definierten Ausgang besitzt.

## Erstinstallation Pi A

Aus dem Repository:

```bash
cd /home/kathapult/thelastgameofmoyn
sudo bash deployment/install-deployment.sh sensorApp
```

Der Installer legt die Konfiguration beim ersten Lauf aus `sensorApp.env.example` an, installiert
alle Rollen-Units und Hilfsskripte, baut das erste Release und aktiviert Hauptdienst sowie
Update-Timer. Derselbe Befehl kann später gefahrlos erneut ausgeführt werden; siehe
„Inkrementelle Installation und Reparatur“.
Vor der Hardware-Abnahme prüfen:

```bash
sudoedit /etc/thelastgameofmoyn/sensorApp.env
systemctl status sensorApp.service sensorApp-update.timer
readlink -f /opt/thelastgameofmoyn/current/sensorApp
```

Beim ersten verwalteten Wechsel übernimmt der Updater eine vorhandene
`sensorApp/bin/data/settings.json` nach `/var/lib/thelastgameofmoyn/sensorApp/settings.json`. Er
kopiert sie nach dem Stop des alten Prozesses ein letztes Mal, damit eine während des Builds
gespeicherte Kalibrierung nicht verloren geht. Spätere Releases verwenden ausschließlich die
Datei unter `/var/lib`.

In `/etc/thelastgameofmoyn/sensorApp.env` muss außerdem das OSC-Ziel der direkten Verbindung
stehen:

```text
OSC_TARGET_IP=10.0.0.2
```

## Erstinstallation Pi B und Beamerkonfiguration

Der erste Aufruf legt `/etc/thelastgameofmoyn/visualApp.env` an. Ist der persistente KMS-Modus noch
nicht vorhanden, beendet sich der Installer absichtlich vor Installation neuer Unit-Dateien:

```bash
cd /home/kathapult/thelastgameofmoyn
sudo bash deployment/install-deployment.sh visualApp
sudoedit /etc/thelastgameofmoyn/visualApp.env
```

Die Werte müssen zum realen Aufbau passen:

```text
DISPLAY=:0
XAUTHORITY=/home/kathapult/.Xauthority
OSC_EXPECTED_SOURCE_IP=10.0.0.1
PREVIEW_TIMEOUT_SECONDS=1.0
PROJECTOR_OUTPUT=HDMI-1
PROJECTOR_KMS_CONNECTOR=HDMI-A-1
PROJECTOR_MODE=1024x768
PROJECTOR_RATE=60
PROJECTOR_EXCLUSIVE=1
```

`PROJECTOR_OUTPUT` ist der Name in `xrandr --query`, `PROJECTOR_KMS_CONNECTOR` der Kernel-/DRM-Name
für die KMS-Kommandozeile. `OSC_EXPECTED_SOURCE_IP` beschränkt beide Diagnose-/Objektempfänger auf
Pi A. `PREVIEW_TIMEOUT_SECONDS` bestimmt, wie lange nur im Kalibrierungsprofil der letzte
vollständige Preview-Frame sichtbar bleibt. Die eingecheckten Werte sind explizite
Installationsvorgaben, aber erst
nach folgender Prüfung am realen Pi freigegeben:

```bash
DISPLAY=:0 XAUTHORITY=/home/kathapult/.Xauthority xrandr --query
ls -1 /sys/class/drm
```

Nach der Kontrolle schreibt der Installer den erforderlichen KMS-Parameter atomar und legt zuvor
eine datierte Sicherung von `cmdline.txt` an:

```bash
sudo bash deployment/install-deployment.sh visualApp --apply-kms
sudo reboot
```

Nach dem Neustart wird die Installation abgeschlossen:

```bash
cd /home/kathapult/thelastgameofmoyn
sudo bash deployment/install-deployment.sh visualApp
systemctl status visualApp.service visualApp-update.timer
readlink -f /opt/thelastgameofmoyn/current/visualApp
```

Der KMS-Helfer verwaltet nur den `video=`-Eintrag für den konfigurierten Connector. Er ersetzt
bereits vorhandene Einträge desselben Connectors und lässt alle anderen Kernelparameter unverändert.
`cmdline.txt` muss dabei weiterhin genau eine nicht leere Zeile enthalten.

XRandR und KMS bleiben stets in der nativen Beamerlage `1024x768` und XRandR-Rotation `normal`.
Die Drehung des Inhalts geschieht erst in `visualApp`; dadurch lassen sich 0°, 90° rechts, 180° und
90° links im laufenden Kalibrierungsmodus prüfen, ohne X11 neu zu konfigurieren. Bei einer bereits
installierten Pi-B-Umgebung bleibt ein alter `PROJECTOR_MODE` wegen der inkrementellen
Standortmigration absichtlich erhalten. Er muss einmal manuell auf `1024x768` geändert und danach
der KMS-Schritt mit `--apply-kms` samt Neustart erneut ausgeführt werden.

## Inkrementelle Installation und Reparatur

`install-deployment.sh` ist nicht auf einen leeren Pi beschränkt. Derselbe Rollenbefehl wird für eine
Erstinstallation, zum Nachinstallieren neuer Units und zur Reparatur einer bestehenden Installation
verwendet:

```bash
sudo bash deployment/install-deployment.sh sensorApp   # auf Pi A
sudo bash deployment/install-deployment.sh visualApp   # auf Pi B
```

Ein vorheriges manuelles `systemctl stop` ist nicht erforderlich. Der Installer hält zunächst den
Update-Timer der gewählten Rolle an, damit während Bootstrap, Fetch und Build kein zweiter
Timer-Lauf konkurriert. War der zugehörige Update-Oneshot bereits aktiv, wartet der Installer auf
dessen Update-Lock. Der bisherige App-Prozess läuft währenddessen weiter. Unmittelbar vor der
atomaren Aktivierung beendet der Updater alle bereits installierten Laufzeitprofile gemeinsam: den
Produktions- und Kalibrierungsdienst sowie auf Pi B zusätzlich den Diagnosedienst. Der Timer wird in
dieselbe systemd-Stop-Transaktion aufgenommen. Dadurch können insbesondere die Rückkehraktionen von
`visualApp-diagnostics.service` weder den Produktionsdienst noch den Timer zwischen Stop und
Symlinkwechsel erneut starten.

Nach erfolgreicher Installation laufen wieder der Produktionsdienst und der Update-Timer. Scheitert
bereits Fetch oder Build, wurde der laufende App-Dienst noch nicht beendet. Scheitert die spätere
Aktivierung, verwendet der Updater den bestehenden automatischen Rollback; ein vor dem Aufruf aktiver
Update-Timer wird vom Installer auch auf dem Fehlerpfad wieder gestartet.

Ein vollständiger Lauf prüft und installiert für die gewählte Rolle jeweils:

- Produktionsdienst, Kalibrierungsdienst, Update-Dienst und Update-Timer sowie auf Pi B den
  On-Demand-Diagnosedienst,
- Updater, Update-Diagnose, Bootstrap- und Rolleninstaller, X11-Wartehelfer und
  Kompatibilitätswrapper,
- auf Pi B zusätzlich XRandR-/KMS-Helfer,
- die aktuelle `<app>.env.example` als Referenz unter `/etc/thelastgameofmoyn`.

Eine vorhandene `/etc/thelastgameofmoyn/<app>.env` wird nicht durch Release-Defaults ersetzt. Der
Installer behält vorhandene Werte und Kommentare bei und hängt nur Zuweisungen für Schlüssel an, die
in einer neueren Example-Datei hinzugekommen sind. Entfernte oder umbenannte Schlüssel werden nicht
automatisch gelöscht. Nach einem Update mit neuen Environment-Schlüsseln müssen die ergänzten Werte
daher fachlich geprüft und bei Bedarf an den realen Aufbau angepasst werden.

Der Rolleninstaller gehört selbst zu jedem Release. Dadurch installiert bereits das inkrementelle
Update, das eine neue Unit oder einen neuen Helfer einführt, den vollständigen neuen Payload. Auch bei
unverändertem Git-Commit gleicht der minütliche Updater die Dateien des aktiven Releases erneut ab.
Dateien mit bereits identischem Inhalt, Eigentümer und Modus werden dabei nicht neu geschrieben.
Manuelle Änderungen an Units und Hilfsskripten unter `/etc/systemd/system` beziehungsweise
`/usr/local` sind deshalb nicht dauerhaft; Standortwerte gehören ausschließlich in die aktive
Environment-Datei.

## Internetfreigabe über den Pi-Direktlink nur im Notfall

Diese Internetfreigabe ist ausschließlich eine On-Demand-Notfallmaßnahme. Im stabilen Normalbetrieb
wird sie nicht eingerichtet. Weder Rolleninstaller noch Updater, Update-Timer oder ein anderer
Produktionsdienst installieren oder starten das Skript; eine Änderung an dem Skript löst für sich
allein auch keinen App-Build und keinen Neustart aus. Erst der unten gezeigte, bewusste Aufruf mit
`--apply` verändert NetworkManager, Routing, IPv4-Forwarding, nftables und systemd.

Die Internetfreigabe ändert den physischen Aufbau nicht. Beide Pis bleiben per WLAN mit demselben
Android-Hotspot verbunden und behalten das vorhandene direkte Ethernet-Kabel. Es werden weder ein
Switch noch ein Router, ein zweiter Netzwerkadapter oder ein weiteres Kabel benötigt:

| Rolle | WLAN am Android-Hotspot | direkter Ethernet-Link |
|---|---|---|
| Pi A / `sensorApp` | `10.130.131.35` | `10.0.0.1/24` |
| Pi B / `visualApp` | `10.130.131.156` | `10.0.0.2/24` |

Das Notfallskript bleibt bis dahin eine inerte Datei im Repository beziehungsweise Release-Payload;
der Rolleninstaller kopiert es nicht nach `/usr/local` und ruft es nicht auf. Es erkennt die Rolle
an der bereits aktiven `10.0.0.x/24`-Adresse. Im Notfall wird zuerst der Pi eingerichtet, dessen WLAN
gerade sicher Internetzugang hat; so kann der zweite Pi bei Bedarf das Paket `nftables` bereits über
diesen Weg installieren. Der erste Aktivierungsaufruf erfolgt auf jedem Pi ausdrücklich aus dem
Git-Checkout:

```bash
cd /home/kathapult/thelastgameofmoyn

# zuerst auf dem Pi mit funktionierendem WLAN, danach auf dem anderen Pi:
sudo bash deployment/configure-internet-sharing.sh --apply
```

Nur dieser bewusste `--apply`-Lauf installiert den Verwaltungshelfer und den zugehörigen Boot-Dienst.
Danach wird die eingerichtete Konfiguration auf beiden Pis geprüft mit:

```bash
sudo /usr/local/sbin/thelastgameofmoyn-configure-internet-sharing --check
```

Existieren mehrere gespeicherte WLAN-Profile und ist keines aktiv, muss das richtige Profil beim
ersten Lauf eindeutig angegeben werden:

```bash
cd /home/kathapult/thelastgameofmoyn
sudo env WIFI_CONNECTION='Name des Hotspot-Profils' \
  bash deployment/configure-internet-sharing.sh --apply
```

Das Skript verändert am WLAN-Profil ausschließlich Autoconnect, Wiederholungsversuche und die
Routenmetrik. IPv4-Methode, Adresse und Gateway werden vor dem Eingriff gespeichert und bei jeder
Prüfung unverändert erwartet. Bei aktivem WLAN ist die jeweilige Adresse `10.130.131.35` oder
`10.130.131.156` eine harte Prüfung. Verwendet das Profil DHCP, muss diese feste Zuordnung im
Android-Hotspot anhand der WLAN-MAC-Adresse reserviert sein; das Skript kann eine DHCP-Reservierung
des Telefons weder erzeugen noch garantieren. Bei manuell konfiguriertem IPv4 muss die erwartete
Adresse bereits im Profil stehen. Eine abweichende aktive WLAN-Adresse bricht `--apply` ab.

Betrieblich richtet jeder Pi Folgendes ein:

- Die eigene WLAN-Standardroute erhält Metrik `100`; sie bleibt damit der bevorzugte Weg.
- Über `eth0` kommt eine Ersatz-Standardroute zum anderen Pi mit Metrik `2000` hinzu.
- `net.ipv4.ip_forward=1` und eine eigene nftables-Tabelle erlauben nur neue Verbindungen vom
  Pi-Link zum WLAN sowie zugehörige Antwortpakete; aus dem Hotspot wird kein neuer Weiterleitungspfad
  zum Pi-Link geöffnet.
- Nur Verkehr aus `10.0.0.0/24`, der den jeweiligen Pi über `wlan0` verlässt, wird maskiert. Der
  lokale OSC-Verkehr auf UDP 8000/8001/8002 folgt weiterhin der spezifischeren direkten Route und
  bleibt unverändert zwischen `10.0.0.1` und `10.0.0.2`.
- Der Dienst `thelastgameofmoyn-internet-sharing.service` lädt ausschließlich die eigene
  nftables-Tabelle beim Boot. Ein vorhandenes Firewall-Regelwerk wird nicht geleert oder ersetzt.

Die Umschaltung greift, wenn ein WLAN-Profil getrennt oder nicht aktiv ist. Bleibt der Pi zwar mit
dem Hotspot assoziiert, aber der Mobilfunk-Uplink dahinter ist defekt, kann die bevorzugte Route je
nach NetworkManager-Connectivity-Erkennung bestehen bleiben. Sind beide WLAN-Verbindungen weg,
existiert naturgemäß kein Internet-Uplink.

Die festen WLAN-Adressen bleiben bei aktiver Verbindung die normalen SSH-Ziele. Ist beispielsweise
Pi B tatsächlich vom WLAN getrennt, kann `10.130.131.156` in diesem Moment nicht erreichbar sein,
weil die Adresse keinem aktiven Interface gehört. Solange Pi A verbunden ist, bleibt Pi B über den
Direktlink als Sprungziel erreichbar:

```bash
# Pi B über den noch per WLAN erreichbaren Pi A:
ssh -J kathapult@10.130.131.35 kathapult@10.0.0.2

# umgekehrt Pi A über Pi B:
ssh -J kathapult@10.130.131.156 kathapult@10.0.0.1
```

Ein Abnahmetest muss deshalb von einer lokalen Konsole oder über den jeweils anderen Pi erfolgen,
damit die eigene SSH-Sitzung beim bewussten Trennen von `wlan0` nicht verloren geht. Auf jedem Pi
sind nacheinander `wlan0` trennen, Peer und Internetziel anpingen, WLAN wieder aktivieren und
`--check` ausführen. Zusätzlich sind OSC-Empfang und Quellfilter während beider Richtungen zu
beobachten. Eine Warnung über weitere nftables-Forward-Chains ist ernst zu nehmen: Eine bereits
vorhandene Firewall mit eigener Drop-Policy kann zusätzliche Freigaben benötigen.

Die Änderung lässt sich auf jedem Pi unabhängig zurücknehmen. Dabei werden die vor dem ersten
`--apply` aufgezeichneten NetworkManager-Werte und der ursprüngliche `ip_forward`-Wert
wiederhergestellt; nur die verwaltete nftables-Tabelle, Unit und Konfigurationsdateien werden
entfernt:

```bash
sudo /usr/local/sbin/thelastgameofmoyn-configure-internet-sharing --remove
```

Exit-Code `3` nach `--apply` oder `--remove` bedeutet, dass die persistenten Werte geschrieben
wurden, NetworkManager sie aber nicht unterbrechungsfrei auf das aktive Interface anwenden konnte;
der Pi muss dann vor der Abnahme neu gestartet und mit `--check` geprüft werden.

## Startreihenfolge und Projektorprüfung

Beide Haupt-Units sind über `graphical.target` aktiviert und an `display-manager.service` gebunden.
Sie warten vor jedem Start auf den X11-Socket und die lesbare `.Xauthority`. Ein
`systemctl restart display-manager.service` wird als Restart an beide Anwendungen weitergegeben;
nach Rückkehr der grafischen Sitzung starten sie geordnet neu. Ein bloßer Verlust des
Display-Managers stoppt sie ebenfalls. Der Update-Timer versucht einen noch inaktiven Hauptdienst
nach Rückkehr der Sitzung erneut zu starten.

Pi B prüft zusätzlich vor jedem Start:

1. Der konfigurierte XRandR-Ausgang ist verbunden.
2. Die Zielauflösung wird angeboten.
3. Optional werden alle anderen Ausgänge deaktiviert.
4. Zielmodus, Bildrate, XRandR-Rotation `normal` und Primary-Ausgang werden gesetzt und anschließend
   ausgelesen.
5. Screensaver, Blanking und DPMS werden deaktiviert.

Schlägt eine dieser Prüfungen fehl, startet das Binary nicht. Nach 90 Sekunden gilt der Startversuch
als fehlgeschlagen; `Restart=always` versucht ihn mit begrenzter Frequenz erneut. `visualApp` beendet
sich außerdem mit Fehlerstatus, wenn das finale Projektions-FBO, das Universe-FBO oder die
PixelPush-Textur nicht angelegt werden konnte. Ein solcher OpenGL-Ressourcenfehler bleibt damit
nicht als formal laufender schwarzer Modus verborgen.

## Updateablauf

Die Timer starten ungefähr eine Minute nach dem Boot und danach minütlich den jeweiligen
root-eigenen Oneshot-Dienst. Root orchestriert nur Releaseverzeichnis, Unit-Installation,
Symlinkwechsel und `systemctl`; `git`, Worktree und `make` laufen explizit als `kathapult`.

Ein Update durchläuft diese Reihenfolge:

1. `origin/main` wird als `kathapult` aktualisiert.
2. Änderungen werden gegen app-spezifische Pfade geprüft. Eine reine `visualApp`-Änderung baut Pi A
   nicht neu und umgekehrt; `shared/` und gemeinsame Deploymentdateien betreffen beide Rollen.
3. Der Zielcommit wird in einem temporären, detached Git-Worktree gebaut. Der Showprozess läuft
   währenddessen unverändert weiter.
4. Das vollständige `bin/` und die Deploymentdateien werden in ein Stagingverzeichnis auf demselben
   Dateisystem kopiert und erst vollständig als Release veröffentlicht.
5. Der Hauptdienst wird gestoppt, der vollständige Rollen-Payload einschließlich aller Units,
   Hilfsskripte und Environment-Defaults wird inkrementell installiert, `daemon-reload` wird
   ausgeführt und der `current`-Symlink wird atomar gewechselt.
6. Erfolg gilt nach fünf Sekunden erst, wenn der Dienst aktiv ist, eine echte `MainPID` besitzt und deren
   `/proc/<pid>/exe` sowie Arbeitsverzeichnis exakt auf das neue Release zeigen.
7. Schlägt Aktivierung oder Prüfung fehl, werden Deploymentdateien, Symlink und Prozess automatisch
   auf das vorherige vollständige Release zurückgesetzt.

Ein Buildfehler stoppt den laufenden Dienst nicht. Der Updater verändert den Arbeitsstand des
Haupt-Checkouts nicht und verwendet weder `git pull` noch `git reset --hard`. Ein pro App geführter
Lock verhindert konkurrierende Timer-, Installer- und manuelle Updateaufrufe. Beim allerersten
Wechsel aus der bisherigen In-place-Installation sichert der Updater zusätzlich die vorhandene
Haupt-Unit sowie Pfad und Arbeitsverzeichnis ihres laufenden Prozesses. Ein Aktivierungsfehler stellt
diese Unit wieder her und prüft, dass der Altprozess erneut aus denselben Pfaden läuft. Auf einem
vollständig frischen Pi ohne vorherigen Dienst existiert naturgemäß kein Altprozess für einen
Rollback; dort bleibt der Installationsfehler sichtbar, ohne einen unbeteiligten Stand zu löschen.

Status und Logs:

```bash
systemctl status sensorApp-update.timer visualApp-update.timer
systemctl list-timers
journalctl -u sensorApp-update.service
journalctl -u visualApp-update.service
journalctl -t sensorApp-update -g 'SUCCESS event='
journalctl -t visualApp-update -g 'SUCCESS event='
```

Die Update-Units schreiben bewusst in das systemd-Journal und nicht in eine eigene Datei unter
`/opt`. Das Journal übernimmt Zeitstempel, Rechte und Rotation. Zusätzlich enthält jede
Updaterzeile einen lokalen Zeitstempel mit Zeitzonenoffset. Nach dem atomaren Veröffentlichen eines
neu gebauten Releases erscheint `SUCCESS event=release-created`; erst nach erfolgreichem
Symlinkwechsel, Dienststart, Prozesspfadprüfung und Schreiben der Zustandsdateien erscheint
`SUCCESS event=update-activated`. Die Identifier `sensorApp-update` und `visualApp-update` erlauben
die rollenbezogene Filterung mit `journalctl -t`.

Ein sofortiger, rollenbezogener Lauf ist möglich mit:

```bash
sudo systemctl start sensorApp-update.service
sudo systemctl start visualApp-update.service
```

### Automatische Update-Diagnose

Der Rolleninstaller legt das gemeinsame Diagnosewerkzeug unter
`/usr/local/sbin/thelastgameofmoyn-diagnose-updates` ab. Ohne Zusatzoptionen arbeitet es lesend und
prüft auf dem jeweiligen Pi alle tatsächlich installierten Rollen. Die nicht installierte Gegenrolle
wird im Modus `all` nur genannt und übersprungen:

```bash
sudo /usr/local/sbin/thelastgameofmoyn-diagnose-updates all
sudo /usr/local/sbin/thelastgameofmoyn-diagnose-updates sensorApp  # ausdrücklich nur Pi A
sudo /usr/local/sbin/thelastgameofmoyn-diagnose-updates visualApp  # ausdrücklich nur Pi B
```

Geprüft und bewertet werden systemd-Zustand, Timeraktivierung, Oneshot-Ergebnis, Unit-Vertrag,
Journal, Git-/SSH-Zugriff auf `origin/main` als `kathapult`, freier Speicher, verwalteter
Release-Symlink, Zustands-Commits, installierte Deploymentdateien sowie `MainPID`, ausführbarer Pfad
und Arbeitsverzeichnis des Produktionsdienstes. Die Zusammenfassung unterscheidet `OK`, Warnungen
und blockierende Fehler. Exit-Code `0` bedeutet, dass kein blockierender Fehler erkannt wurde;
Warnungen können trotzdem vorhanden sein. Exit-Code `1` bedeutet mindestens einen blockierenden
Fehler, Exit-Code `2` einen Aufruf- oder Voraussetzungenfehler.

Nur für einen bewusst ausgelösten echten Update-Lauf wird `--trigger` ergänzt:

```bash
sudo /usr/local/sbin/thelastgameofmoyn-diagnose-updates sensorApp --trigger
```

Diese Option ist nicht rein diagnostisch: Der normale Updater kann bauen, aktivieren, den
Produktionsdienst neu starten oder automatisch zurückrollen. Bei aktiver Kalibrierungs- oder
Diagnose-Unit lehnt das Werkzeug den Trigger ab. Mit `--no-network` lässt sich ausschließlich der
ansonsten lesende,
auf 25 Sekunden begrenzte `git ls-remote`-Test überspringen. Vor der ersten Installation kann
dasselbe Skript direkt aus dem Checkout mit `sudo bash deployment/diagnose-update-services.sh`
ausgeführt werden; eine dabei gemeldete fehlende installierte Diagnosekopie ist dann erwartbar und
wird durch den Rolleninstaller behoben.

## Expliziter Rollback

Verfügbare Releases und den aktiven Pfad anzeigen:

```bash
ls -1 /opt/thelastgameofmoyn/releases/sensorApp
readlink -f /opt/thelastgameofmoyn/current/sensorApp
```

Ein vollständiger Commitname kann gezielt aktiviert werden:

```bash
sudo /usr/local/sbin/thelastgameofmoyn-update-app sensorApp --rollback COMMIT
```

Für Pi B ist `sensorApp` durch `visualApp` zu ersetzen. Der Rollback installiert auch die zum
Release gehörenden Units und Skripte, prüft den gestarteten Prozess und stellt bei einem Fehler das
zuvor aktive Release wieder her. Sensor- und Projektionskalibrierung unter `/var/lib` werden dabei
nicht zurückgesetzt. Der Timer behält den manuell gewählten Stand, bis ein neuerer relevanter Commit
auf `origin/main` erscheint.

## Produktions-, Diagnose- und Kalibrierungsmodus

Die aktivierten Hauptdienste sind der einzige Bootpfad für die Ausstellung. `sensorApp.service`
startet ohne `--debug`, `--preview` oder `--calibration`. `visualApp.service` startet ohne
`--diagnostics` oder `--calibration`, bindet Port 8001 und 8002 nicht, zeichnet weder HUD noch
Kalibrierungsoberfläche und blendet den OS-Mauszeiger explizit aus.
Die Tasten `d` und `p` aktivieren im Produktionsprozess keine Diagnoseausgabe.

Für eine Wartungssitzung existieren separate Units. Sie besitzen absichtlich keinen
`[Install]`-Abschnitt und können daher nicht als normaler Bootdienst aktiviert werden:

- `visualApp-diagnostics.service` startet Pi B mit `--diagnostics`: Text-HUD und read-only
  Sensor-Preview auf Port 8001 sind aktiv; `d` und `p` schalten beide Anzeigen getrennt um.
- `sensorApp-calibration.service` startet Pi A mit `--calibration`: Masken-, Roh-Tiefen- und
  Track-Telemetrie auf Port 8001 sowie der gefilterte Control-Server auf Port 8002 werden aktiv.
- `visualApp-calibration.service` startet Pi B mit `--calibration`: Das Panel, der bestätigungsbasierte
  Control-Client und beide Diagnoseempfänger sind aktiv. Das read-only-Profil `--diagnostics` bleibt
  für ältere Pi-A-Releases verfügbar und öffnet Port 8002 nicht.

### Diagnose-HUD On-Demand auf Pi B

Der Diagnosedienst ist für eine gezielte Beobachtung des Objektstroms und der Visual-Statuszeilen
gedacht, ohne das schreibende Kalibrierungspanel zu öffnen. Sein Start beendet geordnet eine aktive
`visualApp.service`, `visualApp-update.service`, `visualApp-update.timer` und
`visualApp-calibration.service`, bevor das Release-Binary mit `--diagnostics` ausgeführt wird:

```bash
sudo systemctl start visualApp-diagnostics.service
systemctl status visualApp-diagnostics.service \
  visualApp.service visualApp-calibration.service \
  visualApp-update.service visualApp-update.timer
journalctl -fu visualApp-diagnostics.service
```

Erwartet werden genau ein laufender `visualApp`-Prozess, ein sichtbares HUD sowie inaktive
Produktions-, Update- und Kalibrierungsdienste. `d` blendet das Text-HUD aus und wieder ein; `p`
schaltet den Sensor-Preview unabhängig davon. Für ein Livebild muss Pi A weiterhin ein kompatibles
Preview-Profil senden.

Beim manuellen Stop, einem sauberen Programmende, einem fehlgeschlagenen Start oder einem Absturz
startet systemd Produktionsdienst und Update-Timer automatisch. Die Unit startet sich bewusst nicht
neu:

```bash
sudo systemctl stop visualApp-diagnostics.service
systemctl is-active visualApp.service          # erwartet: active
systemctl is-active visualApp-update.timer     # erwartet: active
```

Ein laufender Update-Oneshot wird beim Diagnosebeginn ausdrücklich beendet. Das unterscheidet diesen
On-Demand-Dienst von der Kalibrierungs-Unit, die ihren Start bei einem laufenden Update verweigert.
Wurde ein Update unterbrochen, anschließend Releasepfad, Zustandsdateien und Journal prüfen:

```bash
sudo /usr/local/sbin/thelastgameofmoyn-diagnose-updates visualApp
```

Der Diagnosedienst wird nur installiert, niemals aktiviert. Nach einem Neustart werden daher wie
bisher ausschließlich Produktionsdienst und Update-Timer gestartet.

Vor dem Start muss der jeweilige Update-Dienst inaktiv sein. Ein bereits laufendes Update wird von
der Kalibrierungs-Unit nicht abgebrochen. Beim Start der Kalibrierung stoppen die deklarierten
Konflikte automatisch den Produktionsdienst und den Update-Timer derselben Rolle.

Auf Pi A:

```bash
systemctl is-active sensorApp-update.service   # erwartet: inactive
sudo systemctl start sensorApp-calibration.service
systemctl status sensorApp-calibration.service
```

Auf Pi B:

```bash
systemctl is-active visualApp-update.service   # erwartet: inactive
sudo systemctl start visualApp-calibration.service
systemctl status visualApp-calibration.service
```

Bleibt der Preview-Stream aus oder bricht er ab, verschwindet ein zuvor empfangenes Maskenbild nach
einer Sekunde. Das gilt getrennt für Roh-Tiefe, Maske und Trackwerte; unvollständige Frames werden
nie veröffentlicht. Meldet ein noch nicht aktualisierter Pi A keinen Control-Zustand, zeigt Pi B
`control unavailable`, bleibt bedienbar und sendet keine unbestätigte Änderung in den Visualpfad.
Roh-Tiefe und Maske erscheinen auf Pi B links/rechts gespiegelt. ROI und Trackmarkierungen folgen
derselben Anzeigeabbildung; die übertragenen Sensorkoordinaten und der Objektstrom bleiben
unverändert.

### Bedienung auf Pi B

Das Panel besitzt während der gesamten Kalibrierung den Tastaturfokus. Unbelegte Tasten verändern
weder ein Visual noch den Modus. Sensorwerte sind immer der zuletzt vollständig von Pi A bestätigte
Zustand. Projektionswerte gehören Pi B, wirken unmittelbar und werden bis zum Speichern als
ungespeichert markiert:

| Taste | Funktion |
| --- | --- |
| `Tab` oder `m` | zur nächsten Gruppe wechseln |
| `Shift` + `Tab` oder `Shift` + `m` | zur vorherigen Gruppe wechseln |
| Pfeile | absoluten Sensorwert ändern oder lokalen Projektionswert unmittelbar verstellen |
| `Shift` + Pfeile | lokalen Zentimeter-, Rundungs- oder Viewportwert fein verstellen (0,1 cm, 0,1 Prozentpunkte beziehungsweise 0,001) |
| `f` | ROI auf den vollständigen Tiefenframe zurücksetzen |
| `c` | nur in `TABLE SURFACE MASK`: leeren Tisch über 15 frische Tiefenframes aufnehmen und atomar speichern |
| `x` | nur in `TABLE SURFACE MASK`: aktive und persistierte Tischmaske löschen |
| `r` oder `Esc` | persistierte Sensor- und Projektionswerte neu laden und Änderungen verwerfen |
| `w` | bestätigte Sensorwerte auf Pi A und lokale Projektionswerte auf Pi B atomar speichern |
| `d` / `p` | Text beziehungsweise Bilder umschalten |
| `v` | ausdrücklich zum nächsten Visual wechseln |

Pro Gruppe sind auch die zuvor nur per JSON erreichbaren Blob-, Tracker-, Hide- und Handpositions-
Werte verfügbar. Während eine Anfrage aussteht, werden weitere Pfeiländerungen konsumiert, aber
nicht gesendet. Nach vier erfolglosen Übertragungen zeigt das Panel einen Timeout; der sekündliche
Settings-Heartbeat kann die Anzeige anschließend ohne Neustart wieder synchronisieren.

In `HAND POSITION` wählt links/rechts die Quelle `BLOB CENTRE`, `PALM CENTRE` oder `INWARD TIP`.
`INWARD TIP` bevorzugt den stabil gemittelten, am weitesten in den Tisch ragenden Bereich einer
eindeutig am Tischrand eintretenden Hand. Mehrdeutige oder kurz fehlende Konturen fallen automatisch
auf Handfläche und danach Schwerpunkt zurück; der Wechsel erzeugt keine künstliche Geschwindigkeit.
Oben/unten verändert die Größe des gemittelten Spitzenbereichs. Vor dem Speichern müssen Hand und
Arm nacheinander über gerade Kanten und Rundungen geführt werden. Pi B zeigt dafür ausgewählte rohe
und geglättete Position. Für die Detailabnahme an einem lokalen Monitor auf Pi A markieren
`--debug --calibration` zusätzlich die orange Spitze, den gelben Eintritt und die türkise
Handfläche. Die Position darf dabei nicht zwischen Fingern oder Quellen flackern. `PALM CENTRE`
bleibt der sichere Ausgangswert, bis diese Abnahme am realen Tisch erfolgreich ist.

Die Sensorgruppe `TABLE SURFACE MASK` wird erst nach dem Einstellen der ROI verwendet. Vor `c` muss
die gesamte ROI für ungefähr eine halbe Sekunde frei von Händen und Gegenständen sein. Pi A mittelt
15 frische Frames; mindestens 70 % der ROI müssen dabei stabile gültige Tiefenwerte liefern. Das
Panel zeigt Fortschritt, gültige Abdeckung und Fehler. Erst nach vollständiger Aufnahme und
atomarem Schreiben ersetzt das neue Modell die bisherige Maske. Ein Fehler lässt eine vorhandene
Maske aktiv. `x` entfernt die Datei bewusst; sie kann nur durch eine neue Aufnahme wiederhergestellt
werden.

Mit aktiver Tischmaske ersetzt die pro Pixel aufgenommene Tischfläche den globalen `farMM`-Wert als
hintere Grenze. `nearMM` bleibt aktiv. `MINIMUM HEIGHT ABOVE TABLE` legt in 2-mm-Schritten fest, wie
viel näher ein Pixel als die Tischreferenz sein muss; der Ausgangswert 15 mm unterdrückt Tiefenrauschen,
kann für Hände dicht über dem Tisch aber vorsichtig reduziert werden. Pixel ohne gültige Referenz
bleiben ausgeschlossen. Nach einer ROI-Änderung muss die Tischmaske neu aufgenommen werden.

Die acht lokalen Gruppen heißen `DISPLAY SIZE CM`, `DISPLAY ROUNDING`, `BASE EDGE`,
`DISPLAY POSITION`, `DISPLAY EXTENT`, `OUTPUT ROTATION`, `SENSOR -> TABLE` und `SENSOR MIRROR`.
Breite und Höhe sind die gemessenen Achsen der logischen Spielfläche. `DISPLAY ROUNDING` stellt den
Radius der kurzen Seiten zwischen 0 und 100 % des größtmöglichen Radius ein; links/rechts verändert
ihn um 1,0 Prozentpunkte, mit `Shift` um 0,1 Prozentpunkte. Die gelbe Hilfe zeigt dabei den
tatsächlich verwendeten gerundeten Umriss. Dieser Pi-B-Wert ändert weder die Sensor-ROI auf Pi A noch
den OSC-Vertrag. Als Grundseite sind alle vier Kanten möglich: oben und unten liegen entlang der
Breite, links und rechts entlang der Höhe. Das Panel kennzeichnet deshalb zusätzlich, ob die gewählte
Grundseite bei den aktuellen Maßen die lange oder kurze Seite ist. Die rote Grundkante, vier
verschiedenfarbige Eckpunkte und die eingeblendeten gemappten Objekt-IDs sind der maßgebliche
Vor-Ort-Test.

Ausgabe- und Sensordrehung sind absichtlich getrennt. `OUTPUT ROTATION` dreht das vollständige
gerenderte Bild im nativen 1024×768-Framebuffer. `SENSOR -> TABLE` transformiert dagegen nur die
normierten Kinect-Koordinaten und deren Geschwindigkeiten vor Übergabe an sämtliche Visuals;
`SENSOR MIRROR` spiegelt anschließend in Tischkoordinaten. So kann die um 90° zur Grundseite
montierte Kinect in beiden Drehrichtungen geprüft werden, unabhängig davon, ob zuletzt eine lange
oder kurze Kante als Grundseite gewählt wird.

`DISPLAY POSITION` und `DISPLAY EXTENT` beschreiben ein achsparalleles, auf den nativen
Beamer-Framebuffer normiertes Rechteck. Zusammen mit den Zentimetermaßen und `DISPLAY ROUNDING`
bestimmt es Größe, Seitenverhältnis, Lage, Vierteldrehung und die symmetrische Rundung der
Spielfläche. Eine freie Vierpunkt-Homographie beziehungsweise Trapezkorrektur ist damit nicht
implementiert; verbleibender Keystone muss mechanisch oder am Beamer korrigiert werden.

### Schreibrechte der dauerhaften Kalibrierung

Atomisches Speichern erzeugt eine temporäre Datei im selben Verzeichnis und benennt sie auf die
jeweilige JSON- oder Binärdatei um. Deshalb benötigt der Dienstbenutzer Schreibrecht auf beide
Verzeichnisse, nicht nur auf die Dateien. Der verwaltete Updater richtet sie bereits als `kathapult` mit dessen
primärer Gruppe und Modus `0750`, die Dateien mit `0640` ein. Beide Kalibrierungs-Units setzen
zusätzlich `UMask=0027`, damit auch die temporäre Datei und ihr per Rename veröffentlichtes Ziel bei
`0640` bleiben. Nach einer älteren oder manuellen Installation prüfen:

```bash
stat -c '%U:%G:%a %n' /var/lib/thelastgameofmoyn/sensorApp \
  /var/lib/thelastgameofmoyn/sensorApp/settings.json \
  /var/lib/thelastgameofmoyn/visualApp \
  /var/lib/thelastgameofmoyn/visualApp/projection-calibration.json
test ! -e /var/lib/thelastgameofmoyn/sensorApp/table-surface-mask.bin || \
  stat -c '%U:%G:%a %n' /var/lib/thelastgameofmoyn/sensorApp/table-surface-mask.bin
test -w /var/lib/thelastgameofmoyn/sensorApp && echo writable
test -w /var/lib/thelastgameofmoyn/visualApp && echo writable
```

Die Prüfung `test -w` muss als `kathapult` erfolgen, zum Beispiel mit
`sudo -u kathapult test -w ...`. Abweichungen werden vorzugsweise durch erneutes Ausführen des
Rolleninstallers beziehungsweise Updaters korrigiert, damit Benutzer und primäre Gruppe nicht
manuell angenommen werden.

Nach der Kalibrierung wird jede Rolle ausdrücklich in den Produktionsbetrieb zurückgeführt:

```bash
# Pi A
sudo systemctl stop sensorApp-calibration.service
sudo systemctl start sensorApp.service sensorApp-update.timer

# Pi B
sudo systemctl stop visualApp-calibration.service
sudo systemctl start visualApp.service visualApp-update.timer
```

Ein Neustart beendet eine laufende Kalibrierung ebenfalls und startet nur die aktivierten
Produktionsdienste und Timer. Parallel laufende Produktions- und Kalibrierungsinstanzen werden durch
die Unit-Konflikte verhindert.

## Hardware-Abnahme

Die statischen Prüfungen im Repository ersetzen diese Tests nicht:

1. Beide Pis kalt starten und `MainPID`, Releasepfad, Arbeitsverzeichnis und Journal prüfen.
2. Pi B einmal mit ein- und einmal mit ausgeschaltetem Beamer booten; danach Ausgang, native
   1024 × 768, XRandR-Rotation `normal` und 60 Hz mit `xrandr --current` bestätigen.
3. `display-manager.service` neu starten und die selbständige Rückkehr beider Apps prüfen.
4. Einen Visualprozess und einen Sensorprozess gezielt beenden; Restart-Limit und Journal prüfen.
5. Einen C++-Buildfehler simulieren und belegen, dass der bisherige Prozess weiterläuft.
6. Einen startfähigen, aber absichtlich fehlschlagenden neuen Release simulieren und den
   automatischen Rollback prüfen.
7. Sensor- und Projektionskalibrierung speichern, Update und Rollback ausführen und die
   unveränderten Dateien unter `/var/lib/thelastgameofmoyn/{sensorApp,visualApp}` vergleichen.
8. Eine nur für die jeweils andere Rolle relevante Änderung veröffentlichen und belegen, dass weder
   Build noch Neustart ausgelöst werden.
9. Mindestens einen vollständigen Neustart der grafischen Sitzung und einen mehrstündigen
   Ausstellungsbetrieb mit dem realen Beamer abnehmen.
10. Beide Kalibrierungsdienste gemeinsam starten, Preview/HUD prüfen und anschließend beide Rollen
    ohne sichtbare Diagnoseausgabe in den Produktionsbetrieb zurückführen.
11. Im Produktionsmodus prüfen, dass Port 8001 auf Pi B nicht gebunden, kein Mauszeiger sichtbar und
    `d`/`p` ohne sichtbare Wirkung sind; danach den Rückfall durch einen Neustart bestätigen.
12. Eine Environment-Datei mit angepassten Standortwerten sichern, einen neuen Example-Schlüssel
    simulieren und belegen, dass nur dieser Schlüssel ergänzt wird und alle vorhandenen Werte erhalten
    bleiben.
13. Auf Pi A `sensorApp-update.timer` und `sensorApp.service` stoppen, auf Pi B
    `visualApp-calibration.service` starten und dann auf Pi A
    `python3 tests/osc_v1_probe.py --send 10.0.0.2` ausführen. Im Diagnose-HUD auf Pi B
    prüfen, dass leerer Endzustand, Gap-/Partial-Zähler und vollständige Preview erscheinen, aber
    niemals ein Teilzustand veröffentlicht wird. Anschließend beide Produktionsdienste und Timer
    wieder starten.
14. Den Objektstrom mit `tcpdump -i eth0 -nn -s0 udp port 8000` erfassen und bestätigen, dass die
    größte UDP-Nutzlast 1372 Byte beträgt und keine IPv4-Fragmente auftreten. Ein Testpaket von einer
    anderen Quelladresse muss den `sourceDrop`-Zähler erhöhen und darf keinen Zustand verändern.
15. Beide neuen Kalibrierungsprofile starten und mit `ss -lunp` bestätigen, dass 8001 nur auf Pi B
    und 8002 auf beiden Pis während der Wartung gebunden sind. Nach Rückkehr zur Produktion darf
    keiner der beiden Ports gebunden bleiben.
16. Jede der dreizehn Sensor- und acht Projektionsgruppen verändern. Sensorwerte müssen bestätigt,
    Projektionswerte unmittelbar sichtbar werden. Anschließend `Esc` zum Verwerfen sowie `w`,
    Dienstneustart, Update und Rollback zum Erhalt aller vorhandenen Kalibrierungsdateien testen.
17. Während einer ausstehenden Änderung die Verbindung unterbrechen: Das Panel muss begrenzt
    wiederholen, Timeout anzeigen und nach Rückkehr über Session/Heartbeat neu synchronisieren. Ein
    Neustart von Pi A muss eine neue Session zeigen; verspätete alte Pakete dürfen nichts ändern.
18. Kinect USB-seitig trennen und belegen, dass `get`, `set`, `reload` und die Fehleranzeige auf
    Port 8002 weiter reagieren, obwohl keine frischen Tiefenframes eintreffen.
19. Roh-Tiefe, Maske, ROI, rohe/geglättete Zentren und alle Klassifikationswerte am Tisch gegen die
    lokale Pi-A-DebugView plausibilisieren. Einen Preview- und Track-Teil gezielt verwerfen und
    bestätigen, dass Pi B kein unvollständiges Bild und keine unvollständige Trackliste publiziert.
20. ROI einstellen, Tisch vollständig leeren und in `TABLE SURFACE MASK` mit `c` aufnehmen. Fortschritt,
    mindestens 70 % Abdeckung und die Datei unter `/var/lib` prüfen. Anschließend eine Hand bis dicht
    über alle Tischbereiche führen, den 15-mm-Abstand vorsichtig kalibrieren und belegen, dass der
    Tisch selbst schwarz bleibt. Aufnahmefehler, Neustart, Update und Rollback dürfen die letzte
    erfolgreiche Maske nicht verlieren; `x` muss sie gezielt entfernen.
21. Alle vier Ausgabedrehungen anhand der Eckfarben prüfen. Sowohl eine lange als auch eine kurze
    Kante als Grundseite wählen und sicherstellen, dass die rote Markierung an der realen Kante
    landet; die endgültige Wahl dokumentieren und speichern.
22. Eine Hand nacheinander von der gewählten Grundseite zu allen vier Ecken und entlang beider
    Achsen bewegen. `SENSOR -> TABLE` und Spiegelungen so einstellen, dass Markerposition und
    Bewegungsrichtung übereinstimmen; anschließend alle vier Visuals auf dieselbe Abbildung prüfen.
23. Auf Pi B `visualApp-diagnostics.service` starten und belegen, dass Produktionsdienst,
    Update-Oneshot, Timer und Kalibrierungsdienst inaktiv sind und genau ein Diagnoseprozess aus dem
    aktiven Release läuft. `d` und `p` prüfen, danach die Unit normal stoppen sowie einen
    fehlgeschlagenen Diagnose-Start und Prozessabsturz kontrolliert simulieren. In allen Fällen
    müssen Produktionsdienst und Timer selbständig zurückkehren. Den Abbruch eines laufenden Updates
    nur in einem Wartungsfenster testen und anschließend Update-Diagnose, Journal, `current`-Symlink
    und Zustands-Commits prüfen.
