{
    function f(a) -> x
    {
        x := g(1)
        function g(b) -> y
        {
            y := mload(b)
        }
    }

    function h(c) -> u
    {
        u := j(c)
        function j(d) -> w
        {
            w := 13
        }
    }

}
// ----
// step: unusedFunctionArgumentPruner
//
// {
//     function g(b) -> y
//     { y := mload(b) }
//     function f() -> x
//     { x := g(1) }
//     function f_1(a) -> x
//     { x := f() }
//     function j() -> w
//     { w := 13 }
//     function j_2(d) -> w
//     { w := j() }
//     function h(c) -> u
//     { u := j_2(c) }
// }
