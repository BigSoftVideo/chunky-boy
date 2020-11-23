const chunky_boy = require("./chunky-boy");
const fs = require("fs");
const util = require("util");

const writeAsync = util.promisify(fs.write);

const SEEK_SET = 0;
const SEEK_CUR = 1;
const SEEK_END = 2;

chunky_boy.whenInitialized(() => {
    console.log("Init done.");
    let startMs = new Date().getTime();
    let outFile = fs.openSync("out.mp4", "w");
    setImmediate(() => {
        //let width = 1280;
        //let height = 720;
        let width = 1280;
        let height = 720;
        setImmediate(() => {
            chunky_boy._encode_video_from_callback(
                ctx,
                width,
                height,
                24,
                5_000_000,
                44100,
                192_000,
                writerCbId,
                getImageCbId,
                -1,
                finishedCbId
            );
        });
        // setTimeout(() => {
        //     fs.closeSync(outFile);
        //     console.log("Closed file.");
        //     chunky_boy.delete_context(ctx).then(() => console.log("Done.")).catch((e) => console.error(e));
        // }, 25_000);
        //let fileCurrPos = 0;
        //let filEndPos = 0;
        let writerCbId = chunky_boy.userJsCallbacks.length;
        chunky_boy.userJsCallbacks[writerCbId] = async function (ptr, len, pos) {
            let bytesWritten = (await writeAsync(outFile, chunky_boy.HEAP8, ptr, len, pos)).bytesWritten;
            //fileCurrPos += bytesWritten;
            //filEndPos = Math.max(filEndPos, fileCurrPos);
            //console.log("Written bytes: " + bytesWritten);
            return bytesWritten;
        };
        let finishedCbId = chunky_boy.userJsCallbacks.length;
        chunky_boy.userJsCallbacks[finishedCbId] = function (result) {
            console.log("JS - Finished with status: " + result);
            let endMs = new Date().getTime();
            fs.closeSync(outFile);
            console.log("JS - Encoding and file write ms: " + (endMs - startMs));
            chunky_boy.delete_context(ctx).then(() => console.log("Done.")).catch((e) => console.error(e));
        };
        let getImageCbId = chunky_boy.userJsCallbacks.length;
        chunky_boy.userJsCallbacks[getImageCbId] = async function (frame_id, buffer, len, linesize) {
            let val = frame_id * 2;
            if (val > 255) {
                console.log("JS - stopping at frame ", frame_id);
                return 1;
            }
            chunky_boy.HEAP8.fill(val, buffer, buffer+len);
            return 0; // Success
        };
        // let seekerCbId = chunky_boy.userJsCallbacks.length;
        // chunky_boy.userJsCallbacks[seekerCbId] = function (offset, whence) {
        //     if (whence === SEEK_SET) {
        //         fileCurrPos = offset;
        //     } else if (whence === SEEK_CUR) {
        //         fileCurrPos += offset;
        //     } else if (whence === SEEK_END) {
        //         fileCurrPos = filEndPos + offset;
        //     } else {
        //         return -1;
        //     }
        //     return fileCurrPos;
        // };
        let ctx = chunky_boy._create_context();
        chunky_boy._start_event_loop(ctx);
    });
});

console.log("Test root");
