
print("Hello, world!");
print(document);

function test_domjs() {
    document._setMutationHandler(function() {
        for (var i = 0; i < arguments.length; i++) {
            print(JSON.stringify(arguments[i]));
        }
    });

    window.location = "http://127.0.0.1/";
}

postMessage([12,34,"Hello!"]);

//test_domjs();
