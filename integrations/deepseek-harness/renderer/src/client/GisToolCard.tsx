import type { ToolCallViewProps } from '@deepseek-ai/dsh-client-ui-tool/client'
import { envelopeFromBlock } from './envelope.ts'

const cardStyle = {
  border: '1px solid rgba(127,127,127,.25)', borderRadius: 10,
  background: 'var(--dsh-color-bg-secondary, rgba(127,127,127,.06))',
  display: 'block', position: 'relative', width: '100%', boxSizing: 'border-box', marginBottom: 12,
} as const

/** Compact chat summary; maps render only in the session-level 地图展示 tab. */
export function GisToolCard({ block, toolName, inspect }: ToolCallViewProps) {
  const envelope = envelopeFromBlock(block)
  if (!('kind' in block)) {
    return <div style={{ ...cardStyle, padding: 12 }}>GIS 工具 {toolName} 正在执行…</div>
  }
  if (envelope === null) {
    return <div style={{ ...cardStyle, padding: 12 }}>GIS 工具 {toolName} 的结果格式不可用</div>
  }
  const provider = typeof envelope.meta.provider === 'string' ? envelope.meta.provider : 'unknown'
  return (
    <section style={cardStyle} data-tool="gis" data-state={block.isError ? 'error' : 'ok'}>
      <div style={{ padding: 12 }}><strong>{envelope.tool}</strong> — {envelope.summary}</div>
      <div style={{ padding: '0 12px 10px', opacity: .7, fontSize: 12 }}>
        provider: {provider} · trace: {envelope.traceId} · 地图请在“地图展示”标签页查看
      </div>
      <div style={{ display: 'flex', gap: 8, padding: 10, minHeight: 24 }}>
        {inspect !== undefined ? <button type="button" onClick={inspect}>Inspect</button> : null}
      </div>
    </section>
  )
}
