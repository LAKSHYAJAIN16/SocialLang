import { useState } from "react";

interface CustomSourcePanelProps {
  initialSource: string;
  error: string | null;
  onLoad: (source: string) => void;
  onCancel: () => void;
}

export function CustomSourcePanel({
  initialSource,
  error,
  onLoad,
  onCancel,
}: CustomSourcePanelProps) {
  const [draft, setDraft] = useState(initialSource);

  return (
    <div className="custom-source">
      <div className="custom-source__header">
        <h2>Paste a .sl program</h2>
        <button type="button" className="text-button" onClick={onCancel}>
          Cancel
        </button>
      </div>
      <textarea
        className="custom-source__textarea"
        value={draft}
        onChange={(e) => setDraft(e.target.value)}
        spellCheck={false}
        rows={16}
      />
      {error && <p className="custom-source__error">{error}</p>}
      <div className="custom-source__actions">
        <button
          type="button"
          className="control-button control-button--accent"
          onClick={() => onLoad(draft)}
        >
          Load
        </button>
      </div>
    </div>
  );
}
