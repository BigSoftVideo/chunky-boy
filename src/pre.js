
let INITIALIZED = false;

Module.userJsCallbacks = [];
Module.onChunkyBoyInitialized = function() {};

/** Returns false if the global state has already been initialized. Returns true otherwise */
Module.initialize = async function(callback) {
    const util = require('util');
    const setImmediateAsync = util.promisify(setImmediate);
    if (INITIALIZED) {
        return false;
    }
    // This is defined in the C code.
    while (Module._private_initialize === undefined) {
        // Wait for the next iteration of the event loop
        await setImmediateAsync();
    }
    Module._private_initialize();
    INITIALIZED = true;
    return true;
}

Module.delete_context = async function(ctx) {
    const util = require('util');
    const setImmediateAsync = util.promisify(setImmediate);
    while (Module._private_try_delete_context(ctx) == 0) {
        // Wait for the next iteration of the event loop
        await setImmediateAsync();
    }
}
