
print("Hello, world!");

document._setMutationHandler(function(mut) {
    print(JSON.stringify(mut));
});

// The network layer sometimes deadlocks; disable for now
//window.location = "http://127.0.0.1/";

postMessage(0, [12,34,"Hello!"]);

setTimeout(function() {print("timeout!!!")}, 500);

