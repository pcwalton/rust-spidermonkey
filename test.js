
print("Hello, world!");


function test_domjs() {
    document._setMutationHandler(function(mut) {
        print(JSON.stringify(mut));
    });

    window.location = "http://127.0.0.1/";
}

postMessage(0, [12,34,"Hello!"]);
//test_domjs();
