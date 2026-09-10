"use client";

interface KnobProps {
  label: string;
  value: number;
  min: number;
  max: number;
  step?: number;
  unit?: string;
  onChange: (value: number) => void;
  disabled?: boolean;
}

/**
 * A labelled slider. Deliberately a range input rather than a rotary knob:
 * this half of the app is for organising and comparing, and a slider is
 * faster to read at a glance and works with a trackpad.
 */
export function Knob({
  label,
  value,
  min,
  max,
  step = 0.1,
  unit = "",
  onChange,
  disabled,
}: KnobProps) {
  return (
    <label className={`knob${disabled ? " disabled" : ""}`}>
      <span className="knob-label">{label}</span>
      <input
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        disabled={disabled}
        onChange={(e) => onChange(Number(e.target.value))}
      />
      <span className="knob-value">
        {value.toFixed(1)}
        {unit}
      </span>
    </label>
  );
}
