

Module.userJsCallbacks = [];

//** This must not be modified by the user. */
Module.PRIVATE_INITIALIZED = false;
/** This must not be modified by the user. Use `whenInitialized`. */
Module.PRIVATE_ON_INITIALIZED = [];

/** If chunky boy is already initialized, the callback will be called on the
 * next iteration of the JS event loop.
 * 
 * If chunky boy is not yet initialized, the callback will be called as soon
 * as chunky boy finishes initialization.
 * 
 * It's valid to call this function multiple times. Every callback passed
 * to this function will all get called when initialization is done.
 */
Module.whenInitialized = function(callback) {
    if (Module.PRIVATE_INITIALIZED) {
        setImmediate(callback);
    } else {
        Module.PRIVATE_ON_INITIALIZED.push(callback);
    }
};

Module.delete_context = async function(ctx) {
    const util = require('util');
    const setImmediateAsync = util.promisify(setImmediate);
    while (Module._private_try_delete_context(ctx) == 0) {
        // Wait for the next iteration of the event loop
        await setImmediateAsync();
    }
}
