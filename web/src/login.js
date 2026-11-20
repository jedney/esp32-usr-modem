// The login page. The form posts to /api/login without any script; this only explains why the
// last attempt failed (the modem redirects back with ?error=) and puts the cursor in the form.
const messages = {
    invalid: 'Invalid username or password.',
    wait: 'Too many attempts. Wait a second, then try again.',
    busy: 'Too many browsers are logged in. Log out in another browser, or try again later.',
    unavailable: 'The modem could not record the login. Try again.',
};

export function loginError(search) {
    const code = new URLSearchParams(search).get('error');
    return code !== null && Object.hasOwn(messages, code) ? messages[code] : null;
}

export function startLogin({document, location, history}) {
    const text = loginError(location.search);
    const error = document.getElementById('login-error');
    if (text) {
        error.textContent = text;
        error.hidden = false;
        for (const id of ['username', 'password']) {
            document.getElementById(id).setAttribute('aria-describedby', 'login-error');
        }
        // A reload should not repeat an old error.
        history.replaceState(null, '', location.pathname);
    }
    document.getElementById('username').focus();
}

if (globalThis.document) startLogin(globalThis);
