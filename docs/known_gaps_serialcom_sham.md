# Known gaps and deliberate exclusions — SerialCom + Sham phases

Same convention as the other `docs/known_gaps*.md`. Beam's port of
`BeamV0/GUIMatlab/BEAM/SerialCom/` (8 files) and `Sham/` (3 files).
Seventh/eighth phases.

`SerialCom/` **is** Beam's hardware interface: Beam drives its ultrasound
over USB/serial. The live sonication path
(`GeneralSonication/generalSonicateMaster.m`) builds a text command with
`setSerialCommandFromStimParams` and sends it via `sendSerialCommand`. Beam
does **not** use Verasonics — the VSX code in
`BeamV0/GUIMatlab/BEAM/Stimulation/` is dead legacy (see
`docs/known_gaps_stimulation.md`). All 8 `SerialCom/` files are now ported.

## Ported (`libs/serialcom/`, `libs/sham/`)

- **`setSerialCommandFromStimParams`** (`serialcom/command.{hpp,cpp}`) --
  builds the `"Stim_PR<duty>_PD<ms>_PI<ms>_BD<ms>_BI<ms>_D<s>"` firmware
  command. The MATLAB `num2str` calls (no format arg) are approximated
  with `"%.5g"`, which matches num2str's default 5-significant-digit
  output for the value ranges here (duty 0..1, timings ~1..1000 ms); it is
  not a byte-exact num2str reimplementation.
- **`parseCorrectionReceiveString`** -- only the pure parsing core:
  `split(response, ',')` + `str2double` of 5 fields (unparseable -> NaN,
  fewer than 5 -> throw). The `readline(app.serial)` / `pause` /
  `app.CouplingStatusTextArea` parts are not ported.
- **`whiteNoise`** (`sham/sham_audio.{hpp,cpp}`) -- `2*(rand(...) - 0.5)`.
  Takes an explicit `seed` (MATLAB draws from the global stream; there's no
  source seed to match, so this is a disclosed choice for testability).
- **`setShamAudio`** -- the signal-assembly core: builds the
  `round(duration*fs)`-sample buffer, an optional `0.005 * whiteNoise`
  bed, and adds the (tiled/truncated) burst sound every `bi` seconds at a
  +0.3 s offset. The `load('sonicationSound.mat')` and `sound()` /
  `audiowrite()` I/O are not ported (`soundData`/`fs` are arguments, the
  buffer is returned). The unused `randFlag` argument is dropped. The
  source's off-by-one burst-placement guard (`burstStart > len - clipLen`
  rather than `>=`) is preserved as-is.

## Serial transport (`serialcom/serial_port.{hpp,cpp}`)

A hand-rolled cross-platform serial layer (no new dependency): Windows
(`CreateFile` + `DCB` + `COMMTIMEOUTS`) and POSIX (`open` + `termios` +
`select`), with a stub `#else` branch. All the `app`-widget lines
(`ConnectedLamp`, `uialert`, `CouplingStatusTextArea`) are dropped.

- **`connectSerial.m`** / **`disconnectSerial.m`** -> `SerialPort` lifetime.
  The ctor does `serialport(port, 115200)` + `configureTerminator("LF")`
  (115200 8N1, LF), throwing like `serialport()` erroring; the dtor closes.
  RAII replaces the `app.serial = []` mutation.
- **`sendSerialCommand.m`** -> `sendSerialCommand(link, cmd)`. Drains first
  (`clearSerial`), returns false on a closed link. **Quirk preserved:** the
  source writes `[cmd,'\n']` where `'\n'` in single quotes is two literal
  chars `\` `n`, then `writeline` adds the real LF -- so the wire bytes are
  `<cmd>\nLF`.
- **`clearSerial.m`** -> `clearSerial(link)`: `readLine()` while
  `bytesAvailable() > 1` (the source's `> 1`, not `> 0`, is kept). Added
  guard: stop on an empty read so a terminator-less fragment can't spin.
- **`isSerialHealthy.m`** -> `isSerialHealthy(link)`: `sendSerialCommand("PING")`,
  read a line, check it starts with `"PONG"` (case-insensitive, 4 chars).
  Closed link or I/O error -> false. The `oldTO` timeout dance (all
  commented out in the source) is not carried.
- **`listAvailableCOMPorts.m`** -> `listAvailableComPorts()`
  (`serialportlist("available")`): Windows enumerates
  `HKLM\...\DEVICEMAP\SERIALCOMM` and keeps the openable ones; POSIX scans
  `/dev/tty{USB,ACM,S}*` + `/dev/{cu,tty}.*`. Empty vector for none (the
  source's `{'No COM ports'}` placeholder is a UI concern, left to the
  caller).

`SerialLink` is an interface so the command flow is unit-tested against an
in-memory fake (no device needed); `SerialPort` is the OS-backed
implementation. No MATLAB-parity target -- platform I/O has no `.m`
counterpart to diff.

## Excluded (Sham)

**`readInRecordedData.m`** -- a bare script: `audioread` of a hardcoded
`C:\Users\Verasonics\...\pulsedWave.m4a`, then `sound(clipData, fs)` on an
undefined `clipData`. Not a function, not runnable as written.

## Build status

`cmake --preset msvc && cmake --build --preset msvc`, **141/141** tests via
`.\build-msvc\tests\Debug\unit_tests.exe` (8 new for the serial transport:
the `\n`+LF write quirk, closed-link no-op, drain, PING/PONG health,
missing-port throw). Run the exe directly -- `ctest` is
Application-Control-blocked on this machine.

## Not yet verified

No MATLAB-parity run. Command-string / parse / `setShamAudio` tests are
hand-computed; the serial transport is tested against an in-memory
`SerialLink` fake (the OS `SerialPort` needs a real device). The `num2str`
-> `"%.5g"` approximation in `setSerialCommandFromStimParams` is the main
thing a real MATLAB comparison would want to check.
