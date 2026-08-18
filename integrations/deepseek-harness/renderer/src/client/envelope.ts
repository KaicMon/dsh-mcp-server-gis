export const ENVELOPE_MARKER = '[gis-result-envelope]\n'

export interface GisVisualization {
  readonly kind: string | null
  readonly inline: boolean
  readonly geojson: Record<string, unknown> | null
  readonly resultId: string | null
  readonly mapUrl: string | null
}

export interface GisEnvelope {
  readonly schemaVersion: '1.0'
  readonly tool: string
  readonly traceId: string
  readonly summary: string
  readonly data: Record<string, unknown>
  readonly visualization: GisVisualization
  readonly meta: Record<string, unknown>
  readonly error?: {
    readonly code: string
    readonly message: string
    readonly retryable: boolean
  }
}

function record(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
}

/** Parse only the explicitly tagged durable projection; unrelated JSON output is ignored. */
export function parseEnvelope(text: string): GisEnvelope | null {
  const marker = text.lastIndexOf(ENVELOPE_MARKER)
  if (marker < 0) return null
  try {
    const value: unknown = JSON.parse(text.slice(marker + ENVELOPE_MARKER.length).trim())
    if (!record(value) || value.schemaVersion !== '1.0' || typeof value.tool !== 'string' ||
        typeof value.traceId !== 'string' || typeof value.summary !== 'string' ||
        !record(value.data) || !record(value.meta) || !record(value.visualization)) return null
    const visualization = value.visualization
    if (typeof visualization.inline !== 'boolean') return null
    return value as unknown as GisEnvelope
  } catch {
    return null
  }
}

/** Flatten the frozen Harness result blocks without consulting live MCP state. */
export function envelopeFromBlock(block: unknown): GisEnvelope | null {
  if (!record(block) || block.kind !== 'tool-result' || !Array.isArray(block.content)) return null
  const text = block.content
    .filter(record)
    .filter(item => item.type === 'text' && typeof item.text === 'string')
    .map(item => item.text as string)
    .join('\n')
  return parseEnvelope(text)
}
