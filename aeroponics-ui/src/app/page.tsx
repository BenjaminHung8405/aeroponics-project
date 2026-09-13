import { redirect } from 'next/navigation';
import { cookies } from 'next/headers';

/**
 * Root page — Smart redirect based on auth state.
 *
 * Server component: reads httpOnly cookie via next/headers.
 * No client-side JS needed for initial redirect.
 *
 * Logic (sprint_4.md §2.2 JWT Auth Flow):
 *  - Has 'access_token' cookie → redirect /dashboard
 *  - No cookie → redirect /login
 */
export default async function RootPage() {
  const cookieStore = await cookies();
  const hasToken = cookieStore.has('access_token');

  if (hasToken) {
    redirect('/dashboard');
  } else {
    redirect('/login');
  }
}
