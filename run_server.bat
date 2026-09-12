@echo off
if not exist storage mkdir storage
build\server.exe -p 4040 -web-port 8080 -root storage -web web
