@echo off
setlocal
cd /d C:\Users\Ali\local-mcp-bridge || exit /b 10
set "PATH=C:\Program Files\nodejs;%PATH%"
set "MCP_ALLOWED_ROOTS=C:\Users\Ali\AI-Employee;C:\Users\Ali\IranPriceScout;C:\Users\Ali\Nova-Audit-pr2-clean-check;C:\Users\Ali\imoxy-net-review-worktree;C:\Users\Ali\local-mcp-bridge;C:\Users\Ali\IranPriceScout-integration;C:\Users\Ali\AI-Employee-attachment-race-check;C:\Users\Ali\AI-Employee-audit-590a;C:\Users\Ali\AI-Employee-main-sync;C:\Users\Ali\AI-Employee-mvp-checkpoint-20261022;C:\Users\Ali\AI-Employee-pr1216-acceptance;C:\Users\Ali\AI-Employee-pr1258-acceptance;C:\Users\Ali\AI-Employee-pr1261-fix;C:\Users\Ali\AI-Employee-support-check;C:\Users\Ali\AI-Employee-verify-main-19dd;C:\Users\Ali\AI-Employee-verify-main-b7b6;C:\Users\Ali\AI-Employee-visual-qa;C:\Users\Ali\AI-Employee-world-mobile-fix-20261022;C:\Users\Ali\AI-Employee-world-smoke"
"C:\Program Files\nodejs\node.exe" server.mjs
exit /b %ERRORLEVEL%