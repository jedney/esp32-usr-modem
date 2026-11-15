// Prefer readable cells. Small viewports retain 80x25 with a fitted font; large
// viewports gain cells instead of merely enlarging the original 80x25 screen.
export function terminalGeometry(width, height, cellWidth, cellHeight) {
    if (![width, height, cellWidth, cellHeight].every(n => Number.isFinite(n) && n > 0)) {
        return {cols: 80, rows: 25};
    }
    const cols = Math.max(80, Math.min(65535, Math.floor(width / cellWidth)));
    const rows = Math.max(25, Math.min(65535, Math.floor(height / cellHeight)));
    return {cols, rows};
}

export function sizeMessage(cols, rows) {
    if (!Number.isInteger(cols) || !Number.isInteger(rows) || cols < 80 || cols > 65535 || rows < 25 || rows > 65535) {
        throw new RangeError('Unsupported terminal size');
    }
    return JSON.stringify({t: 'size', cols, rows});
}
