export const ENVELOPE_MARKER = '[gis-result-envelope]\n';
function record(value) {
    return typeof value === 'object' && value !== null && !Array.isArray(value);
}
/** Parse only the explicitly tagged durable projection; unrelated JSON output is ignored. */
export function parseEnvelope(text) {
    const marker = text.lastIndexOf(ENVELOPE_MARKER);
    if (marker < 0)
        return null;
    try {
        const value = JSON.parse(text.slice(marker + ENVELOPE_MARKER.length).trim());
        if (!record(value) || value.schemaVersion !== '1.0' || typeof value.tool !== 'string' ||
            typeof value.traceId !== 'string' || typeof value.summary !== 'string' ||
            !record(value.data) || !record(value.meta) || !record(value.visualization))
            return null;
        const visualization = value.visualization;
        if (typeof visualization.inline !== 'boolean')
            return null;
        return value;
    }
    catch {
        return null;
    }
}
/** Flatten the frozen Harness result blocks without consulting live MCP state. */
export function envelopeFromBlock(block) {
    if (!record(block) || block.kind !== 'tool-result' || !Array.isArray(block.content))
        return null;
    const text = block.content
        .filter(record)
        .filter(item => item.type === 'text' && typeof item.text === 'string')
        .map(item => item.text)
        .join('\n');
    return parseEnvelope(text);
}
//# sourceMappingURL=envelope.js.map