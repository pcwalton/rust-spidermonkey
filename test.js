
print("Hello, world!");
print(document);

document._setMutationHandler(function() {
    for (var i = 0; i < arguments.length; i++) {
        print(JSON.stringify(arguments[i]));
    }
});

window.location = "http://192.0.43.10/";
