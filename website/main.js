// Screenshot gallery + lightbox. Everything degrades to the first screenshot
// and a plain page if this never runs.
(function () {
  'use strict';

  var shot     = document.getElementById('shot');
  var caption  = document.getElementById('caption');
  var tabs     = Array.prototype.slice.call(document.querySelectorAll('.tab'));
  var box      = document.getElementById('lightbox');
  var boxImg   = document.getElementById('lightbox-img');
  var boxClose = box ? box.querySelector('.lightbox-close') : null;
  var opener   = document.querySelector('.shot-open');
  var lastFocus = null;

  function select(tab) {
    tabs.forEach(function (t) {
      var on = t === tab;
      t.classList.toggle('is-active', on);
      t.setAttribute('aria-pressed', on ? 'true' : 'false');
    });
    // Set the intrinsic size with the source so the frame never jumps height
    // between a 764px-tall shot and an 845px-tall one.
    shot.width  = tab.dataset.w;
    shot.height = tab.dataset.h;
    shot.src    = tab.dataset.src;
    shot.alt    = tab.dataset.alt;
    caption.textContent = tab.dataset.caption;
  }

  tabs.forEach(function (tab, i) {
    tab.addEventListener('click', function () { select(tab); });
    // Left/Right move along the strip, as with any segmented control.
    tab.addEventListener('keydown', function (e) {
      var step = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
      if (!step) return;
      e.preventDefault();
      var next = tabs[(i + step + tabs.length) % tabs.length];
      next.focus();
      select(next);
    });
  });

  function openBox() {
    if (!box) return;
    lastFocus = document.activeElement;
    boxImg.src = shot.currentSrc || shot.src;
    boxImg.alt = shot.alt;
    box.hidden = false;
    if (boxClose) boxClose.focus();
  }

  function closeBox() {
    if (!box || box.hidden) return;
    box.hidden = true;
    boxImg.src = '';
    if (lastFocus) lastFocus.focus();
  }

  if (opener) opener.addEventListener('click', openBox);
  if (box) box.addEventListener('click', closeBox);
  document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape') closeBox();
  });
})();
