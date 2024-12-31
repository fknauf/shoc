BEGIN {
    OFS = " & "
    ORS = " \\\\\n"
}
{
    $1 = $1
    print
}
