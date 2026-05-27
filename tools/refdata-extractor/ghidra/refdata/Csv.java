// Csv.java -- shared RFC-4180-ish CSV cell quoting for the reference-data
// extractor's sharded table writers.
//
// ONE CONCERN: turn an arbitrary string into a safe CSV cell. Wrap + double
// internal quotes when the cell carries a comma, quote, or newline. C++
// templated symbols contain commas, so this is not optional (the same rule as
// EnumerateFunctions.java's csv()).

package refdata;

public final class Csv {

    private Csv() {}

    /** RFC-4180 cell quoting: wrap + double internal quotes when needed. */
    public static String q(String s) {
        if (s == null) {
            s = "";
        }
        if (s.indexOf(',') >= 0 || s.indexOf('"') >= 0
                || s.indexOf('\n') >= 0 || s.indexOf('\r') >= 0) {
            return '"' + s.replace("\"", "\"\"") + '"';
        }
        return s;
    }
}
