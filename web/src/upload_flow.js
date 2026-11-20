// The image upload flow: preflight, confirmation, login check, sending, result.
// Every browser call is passed in, so Node can run the whole flow in tests.
import {inspectImage, compareVersions} from './upload.js';

export const MAX_IMAGE = 0x7e0000;

/**
 * Returns upload(file) -> 'ignored' | 'cancelled' | 'installed' | 'rejected' | 'lost' | 'failed'.
 * env.state: {edit, csrf, version, upload}; the flow sets state.upload for its whole run, so a
 * second press during the preflight is ignored.
 * env.show(text): status line.   env.setBusy(bool): disables the upload button.
 * env.confirm(text) -> bool.     env.read(file) -> Promise<Uint8Array>.
 * env.login() -> Promise<bool>:  checks the login session is still valid (GET api/auth).
 * env.send(request) -> Promise<{status, text}>, rejects when the connection fails.
 * env.installed(): called after HTTP 200.
 */
export function createUploadFlow(env) {
    const {state, show, setBusy, confirm, read, login, send, installed} = env;
    return async function upload(file) {
        if (!file || !state.edit || !state.csrf || state.upload) return 'ignored';
        state.upload = true;
        setBusy(true);
        try {
            if (file.size > MAX_IMAGE) throw new Error('Image too large');
            show('Checking image…');
            const bytes = await read(file);
            const image = inspectImage(bytes);
            const downgrade = compareVersions(image.version, state.version) < 0;
            let prompt = `Install ${image.target} v${image.version}? The modem will reboot.`;
            if (downgrade) prompt += '\nThis is a downgrade.';
            if (image.target === 'littlefs') {
                prompt += '\nThis replaces sounds, web files and call history. Keep USB available for recovery.';
            }
            if (!confirm(prompt)) {
                show('Upload cancelled');
                return 'cancelled';
            }
            if (!(await login())) throw new Error('Login expired; reconnect before uploading');
            // The socket can have dropped while the person read the prompt.
            if (!state.edit || !state.csrf) throw new Error('Connection lost; reconnect before uploading');
            let result;
            try {
                result = await send({
                    target: image.target,
                    version: image.version,
                    sha256: image.sha256,
                    csrf: state.csrf,
                    downgrade,
                    filesystem: image.target === 'littlefs',
                    bytes,
                    onProgress: (loaded, total) => {
                        show(total ? `Sending ${Math.round(100 * loaded / total)}% — wait for verification`
                                   : 'Sending image…');
                    },
                });
            } catch {
                show('Upload connection lost. Check the console before retrying.');
                return 'lost';
            }
            show(result.text);
            if (result.status === 200) {
                installed();
                return 'installed';
            }
            return 'rejected';
        } catch (error) {
            show(error.message);
            return 'failed';
        } finally {
            state.upload = false;
            setBusy(false);
        }
    };
}

/** The sender for createUploadFlow, on XMLHttpRequest (or a stand-in with the same calls). */
export function xhrSender(createXhr = () => new XMLHttpRequest()) {
    return request => new Promise((resolve, reject) => {
        const xhr = createXhr();
        xhr.open('POST', `/api/update?target=${request.target}&version=${request.version}`);
        xhr.timeout = 190000;
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');
        xhr.setRequestHeader('X-CSRF-Token', request.csrf);
        xhr.setRequestHeader('X-Image-SHA256', request.sha256);
        if (request.downgrade) xhr.setRequestHeader('X-Confirm-Downgrade', request.sha256);
        if (request.filesystem) xhr.setRequestHeader('X-Confirm-Filesystem', request.sha256);
        xhr.upload.onprogress = event => {
            request.onProgress(event.loaded, event.lengthComputable ? event.total : 0);
        };
        xhr.onload = () => resolve({status: xhr.status, text: xhr.responseText});
        xhr.onerror = xhr.ontimeout = () => reject(new Error('Upload connection lost'));
        xhr.send(request.bytes);
    });
}
