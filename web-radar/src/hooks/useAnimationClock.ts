import { useEffect, useState } from 'react';

export function useAnimationClock(active: boolean): number {
  const [now, setNow] = useState(() => performance.now());

  useEffect(() => {
    if (!active) return;
    let frame = 0;
    let previous = 0;
    const update = (timestamp: number) => {
      if (timestamp - previous >= 80) {
        previous = timestamp;
        setNow(timestamp);
      }
      frame = requestAnimationFrame(update);
    };
    frame = requestAnimationFrame(update);
    return () => cancelAnimationFrame(frame);
  }, [active]);

  return now;
}
