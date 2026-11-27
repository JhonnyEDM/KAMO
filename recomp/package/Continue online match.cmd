@echo off
rem Opens KAMO set to continue the last saved online match. Every player in the match starts
rem KAMO this way, then meets in a lobby room (or direct IP) as usual.
start "" "%~dp0KAMO.exe" --continue-online
