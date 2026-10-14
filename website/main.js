// Screenshot carousel + lightbox. Everything degrades to a plain strip of
// three images and a static page if this never runs.
(function () {
  'use strict';

  var carousel = document.getElementById('carousel');
  var track    = document.getElementById('track');
  var slides   = track ? Array.prototype.slice.call(track.querySelectorAll('.slide')) : [];
  var tabs     = Array.prototype.slice.call(document.querySelectorAll('.tab'));
  var caption  = document.getElementById('caption');
  var prev     = carousel ? carousel.querySelector('.carousel-prev') : null;
  var next     = carousel ? carousel.querySelector('.carousel-next') : null;
  var box      = document.getElementById('lightbox');
  var boxImg   = document.getElementById('lightbox-img');
  var boxClose = box ? box.querySelector('.lightbox-close') : null;
  var current  = 0;
  var lastFocus = null;

  // The strip offset that puts slide `i` in the middle of the viewport. The
  // first shot therefore has empty space on its left, the last on its
  // right; the neighbours show through the edge fades.
  function offsetFor(i) {
    var s = slides[i];
    return (carousel.clientWidth - s.offsetWidth) / 2 - s.offsetLeft;
  }

  var offsetX = 0;     // where the strip is right now
  var anim = null;     // the running requestAnimationFrame, if any

  function setOffset(x) {
    offsetX = x;
    track.style.transform = 'translateX(' + Math.round(x) + 'px)';
  }

  // Glide the strip to `target`. Ease-out, so it leaves fast and settles
  // softly, and the time scales with the distance (a one-step slide takes
  // about half a second, a settle after a drag less) so the speed feels
  // the same whatever triggered it.
  function animateTo(target) {
    if (anim) cancelAnimationFrame(anim);
    var from = offsetX;
    var dist = Math.abs(target - from);
    if (dist < 1) { setOffset(target); return; }
    var duration = Math.max(220, Math.min(650, 220 + dist * 0.45));
    var t0 = null;
    function step(now) {
      if (t0 === null) t0 = now;
      var t = Math.min(1, (now - t0) / duration);
      var e = 1 - Math.pow(1 - t, 3);   // ease-out cubic
      setOffset(from + (target - from) * e);
      anim = t < 1 ? requestAnimationFrame(step) : null;
    }
    anim = requestAnimationFrame(step);
  }

  // Put the current shot in the middle: at once (resize, first paint) or
  // with the glide (everything the user triggers).
  function layout(animate) {
    if (!carousel || !slides.length) return;
    if (anim) { cancelAnimationFrame(anim); anim = null; }
    if (animate) animateTo(offsetFor(current));
    else setOffset(offsetFor(current));
  }

  function select(i, animate) {
    if (animate === undefined) animate = true;
    if (!slides.length) return;
    current = Math.max(0, Math.min(slides.length - 1, i));
    slides.forEach(function (s, k) {
      var on = k === current;
      s.classList.toggle('is-active', on);
      s.setAttribute('aria-current', on ? 'true' : 'false');
    });
    tabs.forEach(function (t, k) {
      var on = k === current;
      t.classList.toggle('is-active', on);
      t.setAttribute('aria-pressed', on ? 'true' : 'false');
    });
    if (caption && tabs[current]) caption.textContent = tabs[current].dataset.caption;
    if (prev) prev.disabled = current === 0;
    if (next) next.disabled = current === slides.length - 1;
    layout(animate);
  }

  tabs.forEach(function (tab, i) {
    tab.addEventListener('click', function () { restartAuto(); select(i); });
    // Left/Right move along the strip, as with any segmented control.
    tab.addEventListener('keydown', function (e) {
      var step = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
      if (!step) return;
      e.preventDefault();
      restartAuto();
      var n = (i + step + tabs.length) % tabs.length;
      tabs[n].focus();
      select(n);
    });
  });

  // A side shot slides into the middle; the middle one opens full size.
  // Neither happens on the release of a drag (see below).
  slides.forEach(function (s, i) {
    s.addEventListener('click', function (e) {
      if (justDragged) { e.preventDefault(); return; }
      restartAuto();
      if (i === current) openBox();
      else select(i);
    });
    var img = s.querySelector('img');
    if (img) {
      img.draggable = false;   // the browser's own image drag would steal ours
      if (!img.complete) img.addEventListener('load', function () { layout(false); });
    }
  });

  if (prev) prev.addEventListener('click', function () { restartAuto(); select(current - 1); });
  if (next) next.addEventListener('click', function () { restartAuto(); select(current + 1); });

  // Drag the strip: it follows the pointer while the button is down and,
  // on release, settles on whichever shot ended up nearest the middle. A
  // move under 6px is not a drag, so a plain click still opens / selects.
  var drag = null;          // { x0, base, moved } while a button is down
  var justDragged = false;  // swallow the click that follows a drag release

  if (carousel) {
    carousel.addEventListener('pointerdown', function (e) {
      if (e.pointerType === 'mouse' && e.button !== 0) return;
      if (e.target.closest('.carousel-arrow')) return;
      drag = { x0: e.clientX, base: offsetFor(current), moved: false };
    });

    carousel.addEventListener('pointermove', function (e) {
      if (!drag) return;
      var dx = e.clientX - drag.x0;
      if (!drag.moved) {
        if (Math.abs(dx) < 6) return;
        drag.moved = true;
        restartAuto();
        // Capture only once it is a drag: captured pointers retarget the
        // click too, which would take a plain click away from the slide.
        carousel.setPointerCapture(e.pointerId);
        carousel.classList.add('is-dragging');
        if (anim) { cancelAnimationFrame(anim); anim = null; }
      }
      setOffset(drag.base + dx);
    });

    function endDrag(e) {
      if (!drag) return;
      var d = drag;
      drag = null;
      if (!d.moved) return;
      carousel.classList.remove('is-dragging');
      // Which shot is nearest the middle now? Then animate the rest of the
      // way. A short flick that didn't carry a shot past the halfway mark
      // still goes one step in its direction, which is what a flick means.
      var x = d.base + (e.clientX - d.x0);
      var mid = carousel.clientWidth / 2;
      var best = current, bestDist = Infinity;
      slides.forEach(function (s, i) {
        var c = x + s.offsetLeft + s.offsetWidth / 2;
        var dist = Math.abs(c - mid);
        if (dist < bestDist) { bestDist = dist; best = i; }
      });
      if (best === current) {
        var dx = e.clientX - d.x0;
        if (dx <= -40) best = current + 1;
        else if (dx >= 40) best = current - 1;
      }
      justDragged = true;
      setTimeout(function () { justDragged = false; }, 0);
      select(best);
    }
    carousel.addEventListener('pointerup', endDrag);
    carousel.addEventListener('pointercancel', function () {
      if (!drag) return;
      drag = null;
      carousel.classList.remove('is-dragging');
      layout(true);
    });

    carousel.addEventListener('keydown', function (e) {
      if (e.key === 'ArrowRight') { e.preventDefault(); restartAuto(); select(current + 1); }
      if (e.key === 'ArrowLeft')  { e.preventDefault(); restartAuto(); select(current - 1); }
    });

    window.addEventListener('resize', function () { layout(false); });
  }

  function openBox() {
    if (!box || !slides.length) return;
    var img = slides[current].querySelector('img');
    if (!img) return;
    lastFocus = document.activeElement;
    boxImg.src = img.currentSrc || img.src;
    boxImg.alt = img.alt;
    box.hidden = false;
    if (boxClose) boxClose.focus();
  }

  function closeBox() {
    if (!box || box.hidden) return;
    box.hidden = true;
    boxImg.src = '';
    if (lastFocus) lastFocus.focus();
  }

  if (box) box.addEventListener('click', closeBox);
  document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape') closeBox();
  });

  // Auto-advance every 3 seconds, wrapping round. It holds still while the
  // mouse is over the strip and carries on once it leaves. Any drag, arrow,
  // button, key or click restarts the 3-second count, so the next automatic
  // step never lands right after the visitor's own. Ticks are skipped while
  // the tab is in the background, so nothing piles up.
  var auto = null;
  var hovered = false;   // mouse over the strip: hold still, resume on leave
  function stopAuto() {
    if (auto) { clearInterval(auto); auto = null; }
  }
  function startAuto() {
    if (slides.length < 2 || auto) return;
    auto = setInterval(function () {
      if (document.hidden || hovered) return;
      select((current + 1) % slides.length);
    }, 3000);
  }
  function restartAuto() {
    stopAuto();
    startAuto();
  }
  if (carousel) {
    // mouseenter/leave rather than pointer events: a touch would otherwise
    // count as a hover that never ends.
    carousel.addEventListener('mouseenter', function () { hovered = true; });
    carousel.addEventListener('mouseleave', function () { hovered = false; });
  }

  // First position at once, so the page doesn't open with the strip
  // gliding in from the left.
  select(0, false);
  startAuto();
})();

// Scroll reveal for the feature rows, on the way down only. A row fades in
// when it comes up from below the viewport. It keeps .is-in while it sits
// above the viewport, so scrolling back up shows it at once with no fade,
// and loses it only once it has dropped below the viewport again — which
// is what makes the next scroll down replay the fade.
(function () {
  'use strict';
  var items = Array.prototype.slice.call(document.querySelectorAll('.reveal'));
  if (!items.length) return;
  if (!('IntersectionObserver' in window)) {
    items.forEach(function (el) { el.classList.add('is-in'); });
    return;
  }
  // A page restored mid-scroll has rows above the viewport that were never
  // shown; mark them in without a transition so they don't fade on the
  // way back up.
  items.forEach(function (el) {
    if (el.getBoundingClientRect().bottom < 0) {
      el.classList.add('no-anim', 'is-in');
    }
  });
  requestAnimationFrame(function () {
    items.forEach(function (el) { el.classList.remove('no-anim'); });
  });
  var io = new IntersectionObserver(function (entries) {
    entries.forEach(function (en) {
      if (en.isIntersecting) {
        en.target.classList.add('is-in');
      } else if (en.boundingClientRect.top > 0) {
        en.target.classList.remove('is-in');   // left through the bottom
      }
    });
  }, { threshold: 0.2 });
  items.forEach(function (el) { io.observe(el); });
})();
