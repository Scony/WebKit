function assert(a, e, m) {
    if (a !== e)
        throw new Error(m);
}

function assertArrayContent(a, e) {
    assert(a.length, e.length, "Size of arrays doesn't match");
    for (var i = 0; i < a.length; i++)
        assert(a[i], e[i], "a[" + i + "] = " + a[i] + " but e[" + i + "] = " + e[i]);
}

let obj1 = { };
let array1 = [ ];

let set1 = new Set([1]);
let set2 = new Set([2, 1]);
let set3 = new Set([3, 1]);
let set4 = new Set([1, 2, 3]);
let set5 = new Set([obj1, array1, set1, 3, 1]);
let map1 = new Map([["a", 1], ["b", 2], [obj1, array1]]);
let map2 = new Map([[3, 1], [1, 2]]);

assert(set1.isSupersetOf(set2), false);
assert(set2.isSupersetOf(set1), true);
assert(set1.isSupersetOf(set3), false);
assert(set3.isSupersetOf(set1), true);
assert(set3.isSupersetOf(set2), false);
assert(set2.isSupersetOf(set3), false);
assert(set4.isSupersetOf(set3), true);
assert(set2.isSupersetOf(set4), false);
assert(set2.isSupersetOf(set5), false);
assert(set3.isSupersetOf(set5), false);
assert(set5.isSupersetOf(set3), true);

assert(set3.isSupersetOf(map1), false);
assert(set3.isSupersetOf(map2), true);

try {
    // Not an object
    set1.isSupersetOf(1);
} catch (e) {
    if (e != "TypeError: Set operation expects first argument to be an object")
        throw e;
}

try {
    set1.isSupersetOf({ });
} catch (e) {
    if (e != "TypeError: Set operation expects first argument to have non-NaN 'size' property")
        throw e;
}

try {
    set1.isSupersetOf({ size:NaN });
} catch (e) {
    if (e != "TypeError: Set operation expects first argument to have non-NaN 'size' property")
        throw e;
}

try {
    set1.isSupersetOf({ size: -34787348578345787853478 });
} catch (e) {
    if (e != "RangeError: Set operation expects first argument to have non-negative 'size' property")
        throw e;
}

try {
    set1.isSupersetOf({ size:1 });
} catch (e) {
    if (e != "TypeError: Set.prototype.isSupersetOf expects other.has to be callable")
        throw e;
}

try {
    set1.isSupersetOf({ size:1, has(v) { return v == 1; } });
} catch (e) {
    if (e != "TypeError: Set.prototype.isSupersetOf expects other.keys to be callable")
        throw e;
}

let error = new Error();
try {
    set1.isSupersetOf({ size:1, has(v) { return v == 1; }, keys() { throw error } });
} catch (e) {
    if (e != error)
        throw e;
}

assert(set1.isSupersetOf({ size:1, has(v) { return set1.has(v); }, keys() { assert(arguments.length, 0, "keys() arguments.length"); return set1.keys() } }), true);
assert(set4.isSupersetOf({ size:1, has(v) { return set1.has(v); }, keys() { assert(arguments.length, 0, "keys() arguments.length"); return set1.keys() } }), true);
