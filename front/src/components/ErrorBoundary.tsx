import { Component, type ErrorInfo, type ReactNode } from "react";

interface ErrorBoundaryProps {
  children: ReactNode;
}

interface ErrorBoundaryState {
  error?: Error;
}

export class ErrorBoundary extends Component<ErrorBoundaryProps, ErrorBoundaryState> {
  state: ErrorBoundaryState = {};

  static getDerivedStateFromError(error: Error): ErrorBoundaryState {
    return { error };
  }

  componentDidCatch(error: Error, errorInfo: ErrorInfo) {
    console.error("[vision] render failed", {
      error,
      componentStack: errorInfo.componentStack,
    });
  }

  render() {
    if (!this.state.error) {
      return this.props.children;
    }

    return (
      <main className="flex min-h-[100dvh] items-center justify-center bg-[#f7f8fb] p-4 text-slate-950">
        <section className="w-full max-w-lg rounded-lg border border-rose-200 bg-white p-5 shadow-panel">
          <p className="text-sm font-semibold text-rose-700">VisionTrack render failed</p>
          <p className="mt-2 text-xs leading-5 text-slate-600">
            The page recovered from a runtime error. Check the browser console for the full
            component stack.
          </p>
          <pre className="mt-3 max-h-40 overflow-auto rounded-md bg-slate-950 p-3 text-[11px] leading-5 text-slate-100">
            {this.state.error.message}
          </pre>
          <button
            type="button"
            className="mt-4 inline-flex h-9 items-center justify-center rounded-md bg-slate-950 px-3 text-xs font-semibold text-white"
            onClick={() => window.location.reload()}
          >
            Reload
          </button>
        </section>
      </main>
    );
  }
}
