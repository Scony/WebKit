function setupTestCase(options = {}) {
    if (window.testRunner) {
        testRunner.waitUntilDone();
        testRunner.dumpAsText();
        if (options.disableConsoleLog)
            console.log = () => {};

        if (!window.internals) {
            document.body.innerText = "FAIL: this test case requires internals";
            testRunner.notifyDone();
        } else if (window.internals.getCurrentDamagePropagation() != "Region") {
            document.body.innerText = "FAIL: this test case requires proper damage propagation";
            testRunner.notifyDone();
        }
    }
}

var failure = null;

function assert(condition, failureMessage) {
    if (failure)
        return false;
    if (!condition) {
        failure = failureMessage;
        return false;
    }
    return true;
}

function assertValid(damage) {
    if (!assert(damage, "damage is empty"))
        return;
    assert(damage.isValid, "damage is invalid");
}

function assertRectsEq(damageRects, expectedRects) {
    const rectCompareFunction = (a, b) => {
        for (var i = 0; i < 4; i++) {
            if (a[i] != b[i])
                return a[i] - b[i];
        }
        return 0;
    };
    damageRects.sort(rectCompareFunction);
    expectedRects.sort(rectCompareFunction);
    const damageRectsStr = JSON.stringify(damageRects);
    const expectedRectsStr =  JSON.stringify(expectedRects);
    assert(
        damageRectsStr == expectedRectsStr,
        "damage rects mismatch, expected: " + expectedRectsStr + " but got: " + damageRectsStr
    );
}

function processAnimationFrameSequence(callbackSequence, callbackIndex) {
    if (callbackSequence.length <= callbackIndex) {
        if (window.testRunner) {
            document.body.innerText = "PASS";
            testRunner.notifyDone();
        } else
            console.log("PASS");
        return;
    }
    requestAnimationFrame(() => {
        console.log("Processing requestAnimationFrame callback #" + callbackIndex);
        callbackSequence[callbackIndex]();
        if (failure) {
            if (window.testRunner) {
                document.body.innerText = "FAIL: " + failure;
                testRunner.notifyDone();
            } else
                console.log("FAIL: " + failure);
            return;
        }
        processAnimationFrameSequence(callbackSequence, callbackIndex + 1);
    });
}

function allFramesDamages() {
    if (!window.internals)
        return [];
    return _simplifyDamages(window.internals.getDamageDetails());
}

function latestFrameDamage() {
    var damages = allFramesDamages();
    if (damages.length == 0)
        return null;
    return damages.at(-1);
}

function log(entity) {
    console.log(JSON.stringify(entity));
}

function createNewElementWithClass(elementName, className, lambda = (el) => {}) {
    var newElement = document.createElement(elementName);
    newElement.className = className;
    lambda(newElement);
    return newElement;
}

function spawnNewElementWithClass(elementName, className, lambda = (el) => {}) {
    var newElement = createNewElementWithClass(elementName, className, lambda);
    document.body.appendChild(newElement);
    return newElement;
}

function _simplifyDamages(damages) {
    return damages.map(damage => _simplifyDamage(damage));
}

function _simplifyDamage(damage) {
    var obj = {
        isValid: damage.isValid,
        bounds: _simplifyDamageRect(damage.bounds),
        rects: Array.from(damage.rects).map(r => _simplifyDamageRect(r)),
    };
    obj.toStr = function () {
        return JSON.stringify(this);
    };
    return obj;
}

function _simplifyDamageRect(damageRect) {
    return [damageRect.x, damageRect.y, damageRect.width, damageRect.height];
}
