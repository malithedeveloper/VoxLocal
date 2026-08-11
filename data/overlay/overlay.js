(() => {
  'use strict';
  const overlay = document.getElementById('overlay');
  const requester = document.getElementById('requester');
  const speech = document.getElementById('speech');
  let timeout = 0;
  let visibleMilliseconds = 8000;
  let preset = 'minimal';
  let animation = 'slide-up';
  let showName = true;
  let fallbackNameColor = '#53fc18';

  function payloadFrom(event) {
    let value = event.detail ?? event;
    if (value && typeof value === 'object' && 'jsonString' in value) value = value.jsonString;
    if (typeof value === 'string') {
      try { return JSON.parse(value); } catch (_) { return {}; }
    }
    return value && typeof value === 'object' ? value : {};
  }

  function setClasses(extra = []) {
    overlay.className = ['overlay', `preset-${preset}`, `animation-${animation}`, ...extra].join(' ');
    overlay.classList.toggle('hide-name', !showName);
  }

  function configure(data) {
    preset = ['minimal', 'subtitle'].includes(data.preset) ? data.preset : 'minimal';
    animation = ['fade', 'slide-left', 'slide-right', 'slide-down', 'slide-up', 'none'].includes(data.entranceAnimation)
      ? data.entranceAnimation : 'slide-up';
    showName = data.showName !== false;
    fallbackNameColor = /^#[0-9a-f]{6}$/i.test(data.fallbackNameColor || '') ? data.fallbackNameColor : '#53fc18';
    document.documentElement.style.setProperty('--background', data.background || '#000');
    document.documentElement.style.setProperty('--foreground', data.foreground || '#fff');
    document.documentElement.style.setProperty('--font', `${data.fontFamily || 'Inter'}, ui-sans-serif, system-ui, sans-serif`);
    visibleMilliseconds = Math.max(500, Number(data.visibleMilliseconds || 8000));
    setClasses();
  }

  function showFor(milliseconds) {
    window.clearTimeout(timeout);
    requestAnimationFrame(() => overlay.classList.add('visible'));
    timeout = window.setTimeout(() => overlay.classList.remove('visible'), milliseconds);
  }

  function receive(event) {
    const data = payloadFrom(event);
    if (data.type === 'configure') return configure(data);
    if (data.type === 'speech-start') {
      setClasses();
      requester.textContent = data.requester || '';
      speech.textContent = data.text || '';
      const nameColor = /^#[0-9a-f]{6}$/i.test(data.requesterColor || '') ? data.requesterColor : fallbackNameColor;
      document.documentElement.style.setProperty('--name-color', nameColor);
      showFor(Math.max(visibleMilliseconds, Number(data.durationMilliseconds || 0) + 700));
    } else if (data.type === 'status') {
      setClasses(['status', data.enabled ? 'enabled' : 'disabled']);
      requester.textContent = '';
      speech.textContent = data.text || '';
      showFor(3200);
    } else if (data.type === 'speech-stop') {
      overlay.classList.remove('visible');
    }
  }

  window.addEventListener('voxlocal', receive);
})();
