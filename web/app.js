// --- SHARED PAGE HELPERS (loaded by every page) ---
const App = (() => {
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

    return { escape, api, formatDate, formatNumber, formatMoney, quantity, duration, badge, notify, logout, fillSelect, busy };
})();
