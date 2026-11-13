# Conta compacta — 0.1.16

A faixa de sessão foi substituída por um botão de conta na mesma linha do título e do menu. A barra superior deixa de ocupar uma segunda linha.

- Conta com visto verde: sessão iniciada.
- Conta com ponto de exclamação âmbar: sem sessão, é necessário entrar.
- Conta com símbolo de reprodução azul: demonstração, sem sessão no servidor.

Tocar abre os detalhes da conta e o formulário de login. Os nomes completos da conta e do estado mantêm-se nos leitores de ecrã e nas dicas ao passar o rato. A forma do indicador também muda, para não depender apenas da cor. A área de toque mantém 48 px em Android. O estado continua baseado na sessão, não na identidade guardada em cache.

Testes QML em desktop e dimensões Android: conta com/sem sessão, abertura dos detalhes, altura compacta do cabeçalho e regressões existentes. Inspeção visual a 390 e 320 px, incluindo o indicador de atividade visível. Sem avisos QML. Compilação Android física não executada nesta revisão.

Atualização apenas do cliente. Mantém o servidor 0.1.15 e todas as funcionalidades anteriores; não é necessário novo deploy para esta alteração.
