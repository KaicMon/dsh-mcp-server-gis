export declare const ENVELOPE_MARKER = "[gis-result-envelope]\n";
export interface GisVisualization {
    readonly kind: string | null;
    readonly inline: boolean;
    readonly geojson: Record<string, unknown> | null;
    readonly resultId: string | null;
    readonly mapUrl: string | null;
}
export interface GisEnvelope {
    readonly schemaVersion: '1.0';
    readonly tool: string;
    readonly traceId: string;
    readonly summary: string;
    readonly data: Record<string, unknown>;
    readonly visualization: GisVisualization;
    readonly meta: Record<string, unknown>;
    readonly error?: {
        readonly code: string;
        readonly message: string;
        readonly retryable: boolean;
    };
}
/** Parse only the explicitly tagged durable projection; unrelated JSON output is ignored. */
export declare function parseEnvelope(text: string): GisEnvelope | null;
/** Flatten the frozen Harness result blocks without consulting live MCP state. */
export declare function envelopeFromBlock(block: unknown): GisEnvelope | null;
//# sourceMappingURL=envelope.d.ts.map