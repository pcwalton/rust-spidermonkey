
print("Hello, world!");
print(document);

document._setMutationHandler(function() {
    for (var i = 0; i < arguments.length; i++) {
        print(JSON.stringify(arguments[i]));
    }
});

print(window);
window.location = "http://107.21.70.111/";
