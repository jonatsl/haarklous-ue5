@echo off
rem Opens the open-world game as a fullscreen app window for PS4-controller play.
rem A separate browser profile makes the fullscreen flags apply even when the browser is already open,
rem and autoplay is allowed so sound starts when you press X on the controller.
rem F11 leaves fullscreen, Alt+F4 closes the window.
set "PAGE=file:///%~dp0haarklous-game.html"
set "PROFILE=%LOCALAPPDATA%\HaarklousControllerPlay"
set "BROWSER=%ProgramFiles%\Google\Chrome\Application\chrome.exe"
if not exist "%BROWSER%" set "BROWSER=%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe"
if not exist "%BROWSER%" set "BROWSER=%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"
if not exist "%BROWSER%" set "BROWSER=%ProgramFiles%\Microsoft\Edge\Application\msedge.exe"
if not exist "%BROWSER%" (
  echo Chrome or Edge was not found. Open haarklous-game.html in your browser and press F11.
  pause
  exit /b 1
)
start "" "%BROWSER%" --user-data-dir="%PROFILE%" --no-first-run --no-default-browser-check --autoplay-policy=no-user-gesture-required --start-fullscreen --app="%PAGE%" %*
