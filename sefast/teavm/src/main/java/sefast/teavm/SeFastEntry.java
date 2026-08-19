package sefast.teavm;

import diuf.sudoku.Grid;
import diuf.sudoku.Settings;
import diuf.sudoku.SolvingTechnique;
import diuf.sudoku.solver.Solver;
import diuf.sudoku.solver.rules.chaining.Chaining;
import org.teavm.jso.JSExport;
import org.teavm.jso.JSBody;

import java.util.EnumSet;

public final class SeFastEntry {
    private static int configuredMode = -1;

    private SeFastEntry() {}

    public static void main(String[] args) {
        if (args.length == 0)
            return;
        int mode = args.length > 1 ? Integer.parseInt(args[1]) : 1;
        System.out.println(rate(args[0], mode));
    }

    @JSExport
    public static String rate(String puzzle, int mode) {
        try {
            return rateImpl(puzzle, mode);
        } catch (Throwable error) {
            return "ERROR," + error.getClass().getName() + "," + error.getMessage();
        }
    }

    @JSExport
    public static String rateDiag(String puzzle, int mode) {
        Chaining.resetDiagnostics();
        String result = rate(puzzle, mode);
        return result + "," + Chaining.getDiagnostics();
    }

    @JSExport
    public static String rateLowCurrent(String puzzle) {
        try {
            if (!isPuzzle(puzzle)) return "ERROR,input";
            configure(0);
            Chaining.clearRatingCaches();
            Settings settings = Settings.getInstance();
            EnumSet<SolvingTechnique> techniques = settings.getTechniques();
            techniques.remove(SolvingTechnique.NishioForcingChain);
            techniques.remove(SolvingTechnique.MultipleForcingChain);
            techniques.remove(SolvingTechnique.DynamicForcingChain);
            techniques.remove(SolvingTechnique.DynamicForcingChainPlus);
            techniques.remove(SolvingTechnique.NestedForcingChain);
            settings.setTechniques(techniques);

            Grid grid = new Grid();
            grid.fromString(puzzle);
            Solver solver = new Solver(grid);
            solver.rebuildPotentialValues();
            solver.getDifficulty();
            if (solver.difficulty >= 19.9) return "";
            return rating10(solver.difficulty) + ","
                    + rating10(solver.pearl) + ","
                    + rating10(solver.diamond);
        } catch (Throwable error) {
            return "ERROR," + error.getClass().getName() + "," + error.getMessage();
        }
    }

    @JSExport
    public static String rateChainCells(String state, int mode, int multiple,
            int dynamic, int nishio, int level, int nestingLimit, String cells) {
        try {
            configure(mode);
            Chaining.clearRatingCaches();
            Grid grid = new Grid();
            grid.fromRatingState(state);
            Chaining chaining = new Chaining(multiple != 0, dynamic != 0,
                    nishio != 0, level, true, nestingLimit);
            return chaining.getBestHintKeyForCells(grid, cells);
        } catch (Throwable error) {
            return "ERROR," + error.getClass().getName() + "," + error.getMessage();
        }
    }

    @JSExport
    public static String rateStaticBest(String state, int mode) {
        try {
            configure(mode);
            Chaining.clearRatingCaches();
            Grid grid = new Grid();
            grid.fromRatingState(state);
            return new Chaining(false, false, false, 0, true, 0)
                    .getBestHintKey(grid);
        } catch (Throwable error) {
            return "ERROR," + error.getClass().getName() + "," + error.getMessage();
        }
    }

    private static String rateImpl(String puzzle, int mode) {
        if (!isPuzzle(puzzle)) {
            throw new IllegalArgumentException("puzzle must contain 81 characters from . and 1-9");
        }
        configure(mode);
        Chaining.clearRatingCaches();

        Grid grid = new Grid();
        grid.fromString(puzzle);
        Solver solver = new Solver(grid);
        solver.rebuildPotentialValues();
        solver.getDifficulty();
        return rating10(solver.difficulty) + ","
                + rating10(solver.pearl) + ","
                + rating10(solver.diamond);
    }

    private static void configure(int mode) {
        if (mode != 0 && mode != 1) {
            throw new IllegalArgumentException("mode must be 0 (current) or 1 (SE 1.2.1)");
        }
        Settings settings = Settings.getInstance();
        Chaining.setParallelCellChooser((state, cells, multiple, dynamic,
                nishio, level, nestingLimit, selectedMode) -> chooseParallel(
                state, cells, multiple, dynamic, nishio, level, nestingLimit,
                selectedMode));
        Chaining.setNativeClosureChooser((state, onIds, offIds, dynamic, nishio) ->
                chooseNativeClosure(state, onIds, offIds, dynamic, nishio));
        settings.setNumThreads(1);
        settings.setBestHintOnly(true);
        settings.setRevisedRating(0);
        settings.setBatchSolving(0);
        settings.setFCPlus(0);
        settings.setBringBackSE121(mode == 1);
        if (mode == 1) {
            settings.Settings_BBSE121();
            settings.setlkSudokuBUG(false);
            settings.setlkSudokuURUL(false);
        } else {
            settings.Settings_Variants();
            settings.setlkSudokuBUG(true);
            settings.setlkSudokuURUL(true);
        }
        configuredMode = mode;
    }

    @JSBody(params = { "state", "cells", "multiple", "dynamic", "nishio",
            "level", "nestingLimit", "mode" }, script =
            "return typeof globalThis.sefastParallelChoose === 'function' ? "
            + "globalThis.sefastParallelChoose(state, cells, multiple, dynamic, "
            + "nishio, level, nestingLimit, mode) : '-1';")
    private static native String chooseParallel(String state, String cells,
            int multiple, int dynamic, int nishio, int level,
            int nestingLimit, int mode);

    @JSBody(params = { "state", "onIds", "offIds", "dynamic", "nishio" },
            script = "return typeof globalThis.sefastNativeClosure === 'function' ? "
                    + "globalThis.sefastNativeClosure(state, onIds, offIds, dynamic, nishio) : '-1';")
    private static native String chooseNativeClosure(String state, String onIds,
            String offIds, int dynamic, int nishio);

    private static int rating10(double value) {
        return (int)Math.round(value * 10.0);
    }

    private static boolean isPuzzle(String puzzle) {
        if (puzzle == null || puzzle.length() != 81) return false;
        for (int i = 0; i < puzzle.length(); i++) {
            char ch = puzzle.charAt(i);
            if (ch != '.' && (ch < '1' || ch > '9')) return false;
        }
        return true;
    }
}
