// --- SHARED PAGE HELPERS (loaded by every page) ---
const App = (() => {
    // --- Line icons: one SVG sprite, used as <svg class="icon"><use href="#i-name"/></svg> ---
    const SPRITE = `<svg width="0" height="0" style="position:absolute" aria-hidden="true">
        <symbol id="i-bridge" viewBox="0 0 24 24"><path d="M2 16h20"/><path d="M4 16c0-5 3.5-8 8-8s8 3 8 8"/><path d="M8 16v-5M12 16V8M16 16v-5"/></symbol>
        <symbol id="i-grid" viewBox="0 0 24 24"><rect x="3" y="3" width="7" height="9" rx="1.5"/><rect x="14" y="3" width="7" height="5" rx="1.5"/><rect x="14" y="12" width="7" height="9" rx="1.5"/><rect x="3" y="16" width="7" height="5" rx="1.5"/></symbol>
        <symbol id="i-box" viewBox="0 0 24 24"><path d="M21 8l-9-5-9 5 9 5 9-5z"/><path d="M3 8v8l9 5 9-5V8"/><path d="M12 13v8"/></symbol>
        <symbol id="i-truck" viewBox="0 0 24 24"><path d="M2 6h12v10H2z"/><path d="M14 9h4l3.5 3.5V16H14z"/><circle cx="6.5" cy="17.5" r="1.8"/><circle cx="17.5" cy="17.5" r="1.8"/></symbol>
        <symbol id="i-id" viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="14" rx="2"/><circle cx="9" cy="11" r="2.2"/><path d="M5.8 16c.7-1.6 1.9-2.4 3.2-2.4s2.5.8 3.2 2.4"/><path d="M14 10h4M14 13h3"/></symbol>
        <symbol id="i-map" viewBox="0 0 24 24"><path d="M9 4L3 6v14l6-2 6 2 6-2V4l-6 2-6-2z"/><path d="M9 4v14M15 6v14"/></symbol>
        <symbol id="i-book" viewBox="0 0 24 24"><path d="M4 4h11a3 3 0 0 1 3 3v13H7a3 3 0 0 1-3-3V4z"/><path d="M4 17a3 3 0 0 1 3-3h11"/><path d="M8 8h6"/></symbol>
        <symbol id="i-send" viewBox="0 0 24 24"><path d="M3 11l18-8-8 18-2-8-8-2z"/></symbol>
        <symbol id="i-refresh" viewBox="0 0 24 24"><path d="M20 12a8 8 0 1 1-2.3-5.6"/><path d="M20 4v5h-5"/></symbol>
        <symbol id="i-logout" viewBox="0 0 24 24"><path d="M15 4h3a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2h-3"/><path d="M10 16l-4-4 4-4"/><path d="M6 12h10"/></symbol>
        <symbol id="i-sun" viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></symbol>
        <symbol id="i-moon" viewBox="0 0 24 24"><path d="M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5z"/></symbol>
        <symbol id="i-plus" viewBox="0 0 24 24"><path d="M12 5v14M5 12h14"/></symbol>
        <symbol id="i-check" viewBox="0 0 24 24"><path d="M5 12.5l4.5 4.5L19 7.5"/></symbol>
        <symbol id="i-x" viewBox="0 0 24 24"><path d="M6 6l12 12M18 6L6 18"/></symbol>
        <symbol id="i-fullscreen" viewBox="0 0 24 24"><path d="M4 10V4h6M14 4h6v6M20 14v6h-6M10 20H4v-6" stroke-linecap="square" stroke-linejoin="miter"/></symbol>
        <symbol id="i-fullscreen-exit" viewBox="0 0 24 24"><path d="M10 3v7H3M14 3v7h7M21 14h-7v7M3 14h7v7" stroke-linecap="square" stroke-linejoin="miter"/></symbol>
        <symbol id="i-pin" viewBox="0 0 24 24"><path d="M12 21s-6.5-6.2-6.5-11a6.5 6.5 0 0 1 13 0c0 4.8-6.5 11-6.5 11z"/><circle cx="12" cy="10" r="2.3"/></symbol>
    </svg>`;

    function icon(name, extraClass = '') {
        return `<svg class="icon ${extraClass}" aria-hidden="true"><use href="#i-${name}"/></svg>`;
    }

    // Every value shown on a page goes through escape(), so text typed by users can't become HTML or scripts
    function escape(value) {
        return String(value ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
    }

    // Calls the C++ API. Throws an Error with the server's own message; when the login has ended (401)
    // the browser goes back to the login page.
    async function api(path, body) {
        const options = body === undefined
            ? { method: 'GET' }
            : { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) };
        let response;
        try {
            response = await fetch(path, { ...options, credentials: 'same-origin' });
        } catch (networkError) {
            throw new Error('Cannot reach the server. Is ./app running?');
        }
        let data = null;
        try {
            data = await response.json();
        } catch (notJson) {
            // empty body or not JSON: handled below
        }
        if (response.status === 401 && !location.pathname.endsWith('/login.html')) {
            location.href = '/login.html';
            throw new Error('Please log in again');
        }
        if (!response.ok) {
            const error = new Error((data && data.error) || `The server answered ${response.status}`);
            error.status = response.status;
            throw error;
        }
        return data;
    }

    // SQLite stores UTC times like "2026-10-25 14:03:00"; they are shown in the viewer's local time
    function parseTime(text) {
        return text ? new Date(text.replace(' ', 'T') + 'Z') : null;
    }

    function formatDate(text) {
        const date = parseTime(text);
        if (!date || isNaN(date)) return '—';
        return date.toLocaleString('en-IN', { day: '2-digit', month: 'short', year: 'numeric', hour: '2-digit', minute: '2-digit' });
    }

    function formatNumber(value, decimals = 0) {
        return Number(value).toLocaleString('en-IN', { minimumFractionDigits: decimals, maximumFractionDigits: decimals });
    }

    function formatMoney(value) {
        return '₹' + formatNumber(value, 0);
    }

    // 120 bags, 7.5 tonnes, 1 bale
    function quantity(amount, unit) {
        const shown = formatNumber(amount, Number.isInteger(amount) ? 0 : 1);
        return `${shown} ${unit}${amount === 1 ? '' : 's'}`;
    }

    // Time between two stored times, e.g. "2 h 15 min"
    function duration(from, to) {
        const start = parseTime(from), end = parseTime(to);
        if (!start || !end || !(end - start >= 0)) return '—';
        const minutes = Math.round((end - start) / 60000);
        if (minutes < 60) return `${minutes} min`;
        const hours = Math.floor(minutes / 60);
        if (hours < 48) return `${hours} h ${minutes % 60} min`;
        return `${Math.floor(hours / 24)} days ${hours % 24} h`;
    }

    // A label and a colour for every status the server uses
    const STATUSES = {
        pending: ['Waiting for a truck', 'warn'], assigned: ['On a truck', 'info'], delivered: ['Delivered', 'ok'],
        cancelled: ['Cancelled', 'muted'], in_transit: ['On the way', 'info'], returning: ['Returning', 'warn'],
        completed: ['Completed', 'ok'], available: ['Available', 'ok'], on_trip: ['On a trip', 'info'],
        maintenance: ['Maintenance', 'muted'], off_duty: ['Off duty', 'muted'], approved: ['Approved', 'ok'],
        rejected: ['Declined', 'muted'], awaiting: ['Awaiting approval', 'warn'],
    };

    function badge(status) {
        const [label, tone] = STATUSES[status] || [status, 'muted'];
        return `<span class="badge badge-${tone}">${escape(label)}</span>`;
    }

    // A short message in the top corner; errors stay until clicked, other messages fade by themselves
    function notify(message, tone = 'ok') {
        let area = document.getElementById('toast-area');
        if (!area) {
            area = document.createElement('div');
            area.id = 'toast-area';
            area.className = 'toast-area';
            area.setAttribute('role', 'status');
            document.body.appendChild(area);
        }
        const toast = document.createElement('div');
        toast.className = `toast toast-${tone}`;
        toast.textContent = message;
        toast.title = 'Click to close';
        toast.addEventListener('click', () => toast.remove());
        area.appendChild(toast);
        if (tone !== 'error') setTimeout(() => toast.remove(), 6000);
    }

    async function logout() {
        try {
            await api('/api/logout', {});
        } finally {
            location.href = '/login.html';
        }
    }

    // Fills a <select> with options; values and labels are escaped
    function fillSelect(select, items, value, label, placeholder) {
        select.innerHTML = (placeholder ? `<option value="">${escape(placeholder)}</option>` : '') +
            items.map(item => `<option value="${escape(value(item))}">${escape(label(item))}</option>`).join('');
    }

    // Disables a button while its action runs, so a double click can't send the request twice
    async function busy(button, action) {
        if (button.disabled) return;
        button.disabled = true;
        try {
            await action();
        } catch (error) {
            notify(error.message, 'error');
        } finally {
            button.disabled = false;
        }
    }

    // Asks a yes/no question in a TruckSetu box instead of the browser's confirm(), which says "localhost:8080 says".
    // Resolves to true for the action button; the other button, Esc or a click on the dimmed page resolve to false.
    function confirm({ title, message = '', action = 'OK', cancel = 'Cancel', danger = false }) {
        const dialog = document.createElement('dialog');
        dialog.className = 'dialog';
        dialog.setAttribute('aria-labelledby', 'dialog-title');
        if (message) dialog.setAttribute('aria-describedby', 'dialog-message');
        dialog.innerHTML = `<div class="dialog-box">
                <h2 id="dialog-title">${escape(title)}</h2>
                ${message ? `<p class="dialog-message" id="dialog-message">${escape(message)}</p>` : ''}
                <div class="dialog-actions">
                    <button type="button" class="btn btn-secondary" data-answer="no">${escape(cancel)}</button>
                    <button type="button" class="btn ${danger ? 'btn-danger-solid' : 'btn-primary'}" data-answer="yes">${escape(action)}</button>
                </div>
            </div>`;
        const opener = document.activeElement;
        let pressedOutside = false;   // so dragging from inside the box out to the page doesn't count as a click outside
        dialog.addEventListener('pointerdown', event => { pressedOutside = event.target === dialog; });
        dialog.addEventListener('click', event => {
            const button = event.target.closest('[data-answer]');
            if (button) dialog.close(button.dataset.answer);
            else if (pressedOutside && event.target === dialog) dialog.close('no');   // the box fills the dialog, so this is the page around it
        });
        document.body.appendChild(dialog);
        dialog.showModal();
        // A risky action starts on the safe button, so pressing Enter by habit doesn't cancel or decline anything
        dialog.querySelector(`[data-answer="${danger ? 'no' : 'yes'}"]`).focus();
        // Esc closes the box too, with an empty returnValue, which counts as no
        return new Promise(resolve => dialog.addEventListener('close', () => {
            dialog.remove();
            if (opener && opener.isConnected) opener.focus();
            resolve(dialog.returnValue === 'yes');
        }));
    }

    // --- Light / dark mode ---
    // Light is the default. Each page's <head> applies a saved choice before drawing, so there is no flash.
    const THEME_KEY = 'trucksetu-theme';

    function theme() {
        return document.documentElement.dataset.theme === 'dark' ? 'dark' : 'light';
    }

    function setTheme(mode) {
        if (mode === 'dark') document.documentElement.dataset.theme = 'dark';
        else delete document.documentElement.dataset.theme;
        try {
            localStorage.setItem(THEME_KEY, mode);
        } catch (storageBlocked) {
            // private windows may block storage; the switch still works for this page
        }
        updateThemeToggles();
        window.dispatchEvent(new CustomEvent('themechange', { detail: mode }));
    }

    function updateThemeToggles() {
        const dark = theme() === 'dark';
        document.querySelectorAll('[data-theme-toggle]').forEach(button => {
            button.innerHTML = icon(dark ? 'sun' : 'moon') + `<span class="label">${dark ? 'Light' : 'Dark'}</span>`;
            button.setAttribute('aria-label', dark ? 'Switch to light mode' : 'Switch to dark mode');
            button.title = dark ? 'Switch to light mode' : 'Switch to dark mode';
        });
    }

    // Reads a colour token, e.g. token('--blue'), for code that can't use CSS (the map)
    function token(name) {
        return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
    }

    document.body.insertAdjacentHTML('afterbegin', SPRITE);
    document.querySelectorAll('[data-theme-toggle]').forEach(button =>
        button.addEventListener('click', () => setTheme(theme() === 'dark' ? 'light' : 'dark')));
    updateThemeToggles();

    return { icon, escape, api, parseTime, formatDate, formatNumber, formatMoney, quantity, duration, badge, notify, logout, fillSelect, busy, confirm, theme, setTheme, token };
})();
