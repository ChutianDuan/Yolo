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
      <main className="flex min-h-[100dvh] items-center justify-center bg-[#f6f7f9] p-4 text-[#202b3c]">
        <section className="w-full max-w-lg rounded-md border border-[#d9aeb2] bg-[#fbfcfd] p-5">
          <p className="text-sm font-semibold text-[#b43c45]">VisionTrack render failed</p>
          <p className="mt-2 text-xs leading-5 text-[#687386]">
            The page recovered from a runtime error. Check the browser console for the full
            component stack.
          </p>
          <pre className="mt-3 max-h-40 overflow-auto rounded-md border border-[#d9dde5] bg-[#f2f4f7] p-3 text-[11px] leading-5 text-[#4c586d]">
            {this.state.error.message}
          </pre>
          <button
            type="button"
            className="mt-4 inline-flex h-9 items-center justify-center rounded-md bg-[#3559a8] px-3 text-xs font-semibold text-white transition-colors hover:bg-[#2f4f95] focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-[#3559a8] focus-visible:ring-offset-2 active:translate-y-px"
            onClick={() => window.location.reload()}
          >
            Reload
          </button>
        </section>
      </main>
    );
  }
}
